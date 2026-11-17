// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <memory>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "video_core/texture_cache/texture_cache.h"

namespace {
struct Runtime {};
struct ImageView {
    ImageView(Runtime&, VideoCommon::NullImageViewParams) {}
};
struct Sampler {
    Sampler(Runtime&, const Tegra::Texture::TSCEntry&) {}
};
struct Empty {};
struct Params {
    static constexpr bool ENABLE_VALIDATION = false;
    static constexpr bool FRAMEBUFFER_BLITS = false;
    static constexpr bool HAS_EMULATED_COPIES = false;
    static constexpr bool HAS_DEVICE_MEMORY_INFO = false;
    static constexpr bool IMPLEMENTS_ASYNC_DOWNLOADS = false;
    static constexpr bool HAS_MSAA_DOWNLOADS = false;
    using Runtime = ::Runtime;
    using Image = VideoCommon::ImageBase;
    using ImageView = ::ImageView;
    using Sampler = ::Sampler;
    using ImageAlloc = Empty;
    using Framebuffer = Empty;
    using AsyncBuffer = Empty;
    using BufferType = u64;
};
using Cache = VideoCommon::TextureCache<Params>;
}

namespace VideoCommon {
struct TextureCacheTestAccess {
    static ImageId Register(Cache& cache, size_t address_space, DAddr cpu, bool sparse) {
        const auto id = cache.slot_images.insert(NullImageParams{});
        auto& image = cache.slot_images[id];
        image.address_space = address_space;
        image.gpu_addr = 0x100000;
        image.cpu_addr = cpu;
        image.guest_size_bytes = 4096;
        image.flags = ImageFlagBits::Registered;
        image.lru_index = cache.lru_cache.Insert(id, 0);
        const auto storage = cache.getStorageID(address_space).value();
        cache.gpu_page_table_storage[storage * 2][1].push_back(id);
        const auto map = cache.slot_map_views.insert(image.gpu_addr, cpu, 4096, id);
        cache.page_table[cpu >> 20].push_back(map);
        if (sparse) {
            image.flags |= ImageFlagBits::Sparse;
            cache.gpu_page_table_storage[storage * 2 + 1][1].push_back(id);
            cache.sparse_views[id].push_back(map);
        } else {
            image.map_view_id = map;
        }
        return id;
    }
    static void Unregister(Cache& cache, ImageId id) {
        cache.UnregisterImage(id);
    }
    static void Check(const Cache& cache, size_t address_space, ImageId id, DAddr cpu,
                      bool sparse, bool present) {
        const auto storage = cache.getStorageID(address_space).value();
        const auto& gpu = cache.gpu_page_table_storage[storage * 2].at(1);
        CHECK((std::ranges::find(gpu, id) != gpu.end()) == present);
        if (sparse) {
            const auto& pages = cache.gpu_page_table_storage[storage * 2 + 1].at(1);
            CHECK((std::ranges::find(pages, id) != pages.end()) == present);
        }
        CHECK(cache.page_table.at(cpu >> 20).empty() == !present);
    }
};
}

TEST_CASE("Texture removal uses its owner's GPU tables after another address space is bound",
          "[texture_address_space]") {
    const bool sparse = GENERATE(false, true);
    const bool close_owner = GENERATE(false, true);
    ScopedSystemTestContext context;
    Core::System system;
    system.Initialize();
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device{physical};
    Tegra::MemoryManager first{system, device, 32, 1ULL << 31, 16};
    Tegra::MemoryManager second{system, device, 32, 1ULL << 31, 16};
    Tegra::Control::ChannelState a{0};
    Tegra::Control::ChannelState b{1};
    a.memory_manager = &first;
    b.memory_manager = &second;
    a.payload.emplace(system, first, a);
    b.payload.emplace(system, second, b);
    Runtime runtime;
    Cache cache{runtime, device};
    cache.CreateChannel(a);
    cache.CreateChannel(b);
    cache.BindToChannel(a.bind_id);
    using Access = VideoCommon::TextureCacheTestAccess;
    const auto owner = Access::Register(cache, first.GetID(), 0x200000, sparse);
    cache.BindToChannel(b.bind_id);
    const auto other = Access::Register(cache, second.GetID(), 0x300000, sparse);
    if (close_owner) {
        cache.EraseChannel(a.bind_id);
    }
    Access::Unregister(cache, owner);
    Access::Check(cache, first.GetID(), owner, 0x200000, sparse, false);
    Access::Check(cache, second.GetID(), other, 0x300000, sparse, true);
    Access::Unregister(cache, other);
    Access::Check(cache, second.GetID(), other, 0x300000, sparse, false);
}
