// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/shader_info.h"
#include "video_core/texture_cache/types.h"

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
    Common::SlotVector<ImageView> slot_image_views;
    std::vector<u32> image_view_generations;

    [[nodiscard]] const ImageView& GetImageView(ImageViewId id) const {
        return slot_image_views[id];
    }

    void Erase(ImageViewId id) {
        slot_image_views.erase(id);
        BumpImageViewGeneration(id);
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
