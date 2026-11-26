// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/shader_info.h"
#include "video_core/texture_cache/image_view_slots.h"

namespace {
using VideoCommon::ImageViewId;

struct ImageView {
    GPUVAddr gpu_addr;
    u32 handle;

    [[nodiscard]] u32 Handle(Shader::TextureType) const {
        return handle;
    }
};

struct Cache {
    VideoCommon::ImageViewSlots<ImageView> slot_image_views;

    [[nodiscard]] const ImageView& GetImageView(ImageViewId id) const {
        return slot_image_views[id];
    }

    void Erase(ImageViewId id) {
        slot_image_views.erase(id);
    }

#include "oot-effects-view/oot_effects_view_cache.inc"
};

struct Effects {
    std::vector<u32> saved;
    std::vector<u32> marked;

    void SaveEffectsBase(u32 handle, u32, u64) {
        saved.push_back(handle);
    }

    void MarkEffects(u32 handle, u64) {
        marked.push_back(handle);
    }
};

struct Compute {
    ImageViewId destination;

    [[nodiscard]] auto WrittenImages() const {
        return std::array{destination};
    }
};

struct Renderer {
    Cache texture_cache;
    Effects effects;
    Effects* const oot_live_dlss{&effects};
    struct {
        u32 extent{};
    } oot_motion;
    const u64 render_trace_frame = 17;
    std::optional<u64> oot_effects_frame;
    GPUVAddr oot_effects_address{};
    ImageViewId oot_effects_view{};
    u32 oot_effects_view_generation{};
    u32 oot_effects_scene{};

    void Save(ImageViewId scene_id) {
        const auto& scene = texture_cache.GetImageView(scene_id);
#include "oot-effects-view/oot_effects_view_save.inc"
    }

    void Mark(ImageViewId destination) {
        const Compute command{destination};
        const auto* const pipeline = &command;
        const bool marking_effects = oot_effects_frame == render_trace_frame;
#include "oot-effects-view/oot_effects_view_mark.inc"
    }
};

class GenerationMemory final : public std::pmr::memory_resource {
public:
    bool refuse{};
    std::size_t attempts{};

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++attempts;
        if (refuse) {
            throw std::bad_alloc{};
        }
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* data, std::size_t bytes, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(data, bytes, alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

using GenerationAllocator = std::pmr::polymorphic_allocator<u32>;
using TestViewSlots = VideoCommon::ImageViewSlots<std::shared_ptr<u32>, GenerationAllocator>;
} // namespace

TEST_CASE("Ocarina effect marking uses the saved live scene view", "[video_core][oot_effects_view]") {
    Renderer renderer;
    auto& slots = renderer.texture_cache.slot_image_views;
    constexpr GPUVAddr scene_address = 0x1000;
    const auto scene = slots.insert(ImageView{scene_address, 11});
    const auto alias = slots.insert(ImageView{scene_address, 22});
    const auto unrelated = slots.insert(ImageView{0x2000, 33});
    renderer.Save(scene);
    REQUIRE(renderer.effects.saved == std::vector<u32>{11});

    SECTION("An unrelated dispatch leaves the live scene pending") {
        renderer.Mark(unrelated);
        CHECK(renderer.effects.marked.empty());
        CHECK(renderer.oot_effects_frame.has_value());
        renderer.Mark(alias);
        REQUIRE(renderer.effects.marked == std::vector<u32>{11});
    }

    SECTION("Retiring a scene slot prevents marking through another view of the address") {
        renderer.texture_cache.Erase(scene);
        renderer.Mark(alias);
        CHECK(renderer.effects.marked.empty());
    }

    SECTION("A replacement at the same slot and address is a different scene") {
        renderer.texture_cache.Erase(scene);
        const auto replacement = slots.insert(ImageView{scene_address, 44});
        REQUIRE(replacement == scene);
        renderer.Mark(replacement);
        CHECK(renderer.effects.marked.empty());

        renderer.Save(replacement);
        renderer.Mark(alias);
        CHECK(renderer.effects.marked == std::vector<u32>{44});
    }

    CHECK_FALSE(renderer.oot_effects_frame.has_value());
}

TEST_CASE("Retiring a moved view cannot allocate its generation", "[video_core][oot_effects_view]") {
    GenerationMemory memory;
    TestViewSlots slots{GenerationAllocator{&memory}};
    const auto scene = slots.insert(std::make_shared<u32>(17));
    const auto other = slots.insert(std::make_shared<u32>(29));
    const u32 old_generation = slots.Generation(scene);
    const u32 other_generation = slots.Generation(other);
    const std::size_t insertion_attempts = memory.attempts;
    auto moved = std::move(slots[scene]);
    memory.refuse = true;
    REQUIRE_NOTHROW(slots.erase(scene));
    CHECK(*moved == 17);
    CHECK(slots.GetIfCurrent(scene, old_generation) == nullptr);
    const auto same_slot = slots.insert(std::make_shared<u32>(41));
    REQUIRE(same_slot == scene);
    CHECK(memory.attempts == insertion_attempts);
    CHECK(slots.GetIfCurrent(scene, old_generation) == nullptr);
    const auto* replacement = slots.GetIfCurrent(scene, slots.Generation(scene));
    const auto* untouched = slots.GetIfCurrent(other, other_generation);
    REQUIRE(replacement != nullptr);
    REQUIRE(untouched != nullptr);
    CHECK(**replacement == 41);
    CHECK(**untouched == 29);
}

TEST_CASE("A failed generation allocation destroys the unpublished view",
          "[video_core][oot_effects_view]") {
    GenerationMemory memory;
    TestViewSlots slots{GenerationAllocator{&memory}};
    auto view = std::make_shared<u32>(17);
    const std::weak_ptr<u32> unpublished = view;
    constexpr ImageViewId FirstSlot{0};

    memory.refuse = true;
    REQUIRE_THROWS_AS((void)slots.insert(std::move(view)), std::bad_alloc);
    CHECK(unpublished.expired());
    CHECK(slots.GetIfCurrent(FirstSlot, 0) == nullptr);

    memory.refuse = false;
    const auto retry = slots.insert(std::make_shared<u32>(41));
    REQUIRE(retry == FirstSlot);
    const auto* live = slots.GetIfCurrent(retry, slots.Generation(retry));
    REQUIRE(live != nullptr);
    CHECK(**live == 41);
}
