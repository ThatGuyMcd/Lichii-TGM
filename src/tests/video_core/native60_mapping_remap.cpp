// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <exception>
#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "video_core/buffer_cache/virtual_range_cache.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"

namespace {
class MappingRasterizer final : public VideoCore::RasterizerInterface {
public:
    void Draw(bool, u32) override {}
    void DrawTexture() override {}
    void Clear(u32) override {}
    void DispatchCompute() override {}
    void ResetCounter(VideoCommon::QueryType) override {}
    void Query(GPUVAddr, VideoCommon::QueryType, VideoCommon::QueryPropertiesFlags, u32,
               u32) override {}
    void BindGraphicsUniformBuffer(size_t, u32, GPUVAddr, u32) override {}
    void DisableGraphicsUniformBuffer(size_t, u32) override {}
    void SignalFence(std::function<void()>&&) override {}
    void SyncOperation(std::function<void()>&&) override {}
    void SignalSyncPoint(u32) override {}
    void SignalReference() override {}
    void ReleaseFences(bool) override {}
    void FlushAll() override {}
    void FlushRegion(DAddr, u64, VideoCommon::CacheType) override {}
    bool MustFlushRegion(DAddr, u64, VideoCommon::CacheType) override {
        return false;
    }
    VideoCore::RasterizerDownloadArea GetFlushArea(DAddr addr, u64 size) override {
        return {addr, addr + size, false};
    }
    void InvalidateRegion(DAddr, u64, VideoCommon::CacheType) override {}
    void OnCacheInvalidation(DAddr, u64) override {}
    bool OnCPUWrite(DAddr, u64) override {
        return false;
    }
    void InvalidateGPUCache() override {}
    void UnmapMemory(DAddr, u64) override {}
    void ModifyGPUMemory(size_t as_id, GPUVAddr address, u64 size) override {
        changes.emplace_back(address, size);
        ranges.Unmap(as_id, address, size);
    }
    VideoCommon::VirtualRangeCache ranges;
    std::vector<std::pair<GPUVAddr, u64>> changes;
    void FlushAndInvalidateRegion(DAddr, u64, VideoCommon::CacheType) override {}
    void WaitForIdle() override {}
    void FragmentBarrier() override {}
    void TiledCacheBarrier() override {}
    void FlushCommands() override {}
    void TickFrame() override {}
    Tegra::Engines::AccelerateDMAInterface& AccessAccelerateDMA() override {
        std::terminate();
    }
    void AccelerateInlineToMemory(GPUVAddr, size_t, std::span<const u8>) override {}
};


} // namespace

TEST_CASE("Native60: Same-type GPU remaps invalidate cached device spans",
          "[video_core][native60_mapping_remap]") {
    const bool big = GENERATE(false, true);
    const u64 big_bits = GENERATE(16ULL, 17ULL);
    CAPTURE(big, big_bits);
    const u64 page = big ? 1ULL << big_bits : 0x1000;
    constexpr GPUVAddr base = 0x10000000;
    constexpr DAddr first = 0x20000000;
    constexpr DAddr replacement = 0x30000000;
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    MappingRasterizer rasterizer;
    Tegra::MemoryManager memory{system, device_memory, 32, 1ULL << 31, big_bits};
    memory.BindRasterizer(&rasterizer);
    const auto map = [&](GPUVAddr gpu, DAddr device, u64 bytes) {
        memory.Map(gpu, device, bytes, Tegra::PTEKind::PITCH, big);
    };
    const auto query = [&] {
        return *rasterizer.ranges.Query(memory, base, static_cast<u32>(4 * page));
    };
    map(base, first, 4 * page);
    REQUIRE(rasterizer.changes ==
            std::vector<std::pair<GPUVAddr, u64>>{{base, 4 * page}});
    const auto initial = query();
    REQUIRE(initial.size() == 1);
    REQUIRE(initial.front().device_addr == first);
    // Also exercise the new enclosing-span hit before replacing its middle.
    REQUIRE(rasterizer.ranges.Query(memory, base + page, static_cast<u32>(page))
                ->front().device_addr == first + page);
    rasterizer.changes.clear();

    map(base + page, replacement, 2 * page);
    // Both modified pages form one notification, despite unchanged EntryType::Mapped.
    REQUIRE(rasterizer.changes ==
            std::vector<std::pair<GPUVAddr, u64>>{{base + page, 2 * page}});
    const auto remapped = query();
    REQUIRE(remapped.size() == 3);
    CHECK(remapped[0].device_addr == first);
    CHECK(remapped[0].size == page);
    CHECK(remapped[1].device_addr == replacement);
    CHECK(remapped[1].size == 2 * page);
    CHECK(remapped[2].device_addr == first + 3 * page);
    CHECK(remapped[2].size == page);
    CHECK(memory.GpuToCpuAddress(base + 2 * page) == replacement + page);

    rasterizer.changes.clear();
    map(base + page, replacement, 2 * page);
    CHECK(rasterizer.changes.empty());
    REQUIRE(query().size() == 3);

    // Restoring the middle rejoins the span. Identical pages on either side do not notify.
    map(base, first, 4 * page);
    REQUIRE(rasterizer.changes ==
            std::vector<std::pair<GPUVAddr, u64>>{{base + page, 2 * page}});
    REQUIRE(query().size() == 1);
    CHECK(query().front().device_addr == first);

    map(base, replacement, page);
    map(base + 3 * page, replacement + 3 * page, page);
    REQUIRE(query().size() == 3);
    rasterizer.changes.clear();
    // Two changed runs separated by identical mappings must remain separate notifications.
    map(base, first, 4 * page);
    REQUIRE(rasterizer.changes ==
            std::vector<std::pair<GPUVAddr, u64>>{{base, page}, {base + 3 * page, page}});
    REQUIRE(query().size() == 1);
    CHECK(query().front().device_addr == first);
    rasterizer.changes.clear();
    map(base, first, 4 * page);
    CHECK(rasterizer.changes.empty());
}
