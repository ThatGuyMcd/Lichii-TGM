// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <exception>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "tests/video_core/native60_dma_test_access.h"
#include "video_core/control/channel_state.h"
#include "video_core/gpu_split.h"
#include "video_core/memory_manager.h"
#include "video_core/nx2.h"
#include "video_core/rasterizer_interface.h"

namespace {
class GatherRasterizer final : public VideoCore::RasterizerInterface {
public:
    std::atomic<u32> cpu_value{}, cached_value{}, gathers{}, back_gathers{};
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
    bool MustFlushRegion(DAddr, u64, VideoCommon::CacheType) override { return false; }
    VideoCore::RasterizerDownloadArea GetFlushArea(DAddr addr, u64 size) override {
        return {addr, addr + size, false};
    }
    void InvalidateRegion(DAddr, u64, VideoCommon::CacheType) override {}
    void OnCacheInvalidation(DAddr, u64) override {}
    bool OnCPUWrite(DAddr, u64) override { return false; }
    void InvalidateGPUCache() override {
        cached_value.store(cpu_value.load());
        if (VideoCommon::GpuSplit::OnBackThread()) {
            ++back_gathers;
        }
        ++gathers;
    }
    void UnmapMemory(DAddr, u64) override {}
    void ModifyGPUMemory(size_t, GPUVAddr, u64) override {}
    void FlushAndInvalidateRegion(DAddr, u64, VideoCommon::CacheType) override {}
    void WaitForIdle() override {}
    void FragmentBarrier() override {}
    void TiledCacheBarrier() override {}
    void FlushCommands() override {}
    void TickFrame() override {}
    Tegra::Engines::AccelerateDMAInterface& AccessAccelerateDMA() override { std::terminate(); }
    void AccelerateInlineToMemory(GPUVAddr, size_t, std::span<const u8>) override {}
};

class ConsumerEngine final : public Tegra::Engines::EngineInterface {
public:
    explicit ConsumerEngine(GatherRasterizer& rasterizer_) : rasterizer{rasterizer_} {
        execution_mask.set(0x100);
        reports_fast_draw_breaks = true;
    }
    std::vector<u32> observed;
    void CallMethod(Core::System&, u32, u32, bool) override {
        observed.push_back(rasterizer.cached_value.load());
    }
    void CallMultiMethod(Core::System& system, u32 method, const u32* words, u32 count,
                         u32) override {
        for (u32 i = 0; i < count; ++i) {
            CallMethod(system, method, words[i], true);
        }
    }
private:
    GatherRasterizer& rasterizer;
};

Tegra::CommandHeader Inline(Tegra::BufferMethods method, u32 value) {
    return Tegra::BuildCommandHeader(method, value, Tegra::SubmissionMode::Inline);
}

Tegra::CommandList List(bool acquire) {
    boost::container::small_vector<Tegra::CommandHeader, 512> words;
    words.push_back(Inline(static_cast<Tegra::BufferMethods>(0x100), 1));
    if (acquire) {
        words.push_back(Inline(Tegra::BufferMethods::SemaphoreAddressHigh, 0));
        // Inline arguments are only 13 bits; the full address needs a data word.
        words.push_back(Tegra::BuildCommandHeader(Tegra::BufferMethods::SemaphoreAddressLow, 1,
                                                 Tegra::SubmissionMode::Increasing));
        words.push_back(Tegra::CommandHeader{.argument = 0x4000});
        words.push_back(Inline(Tegra::BufferMethods::SemaphoreSequencePayload, 7));
        words.push_back(Inline(Tegra::BufferMethods::SemaphoreOperation, 1));
        words.push_back(Inline(static_cast<Tegra::BufferMethods>(0x100), 2));
    }
    return Tegra::CommandList{std::move(words)};
}
} // namespace

TEST_CASE("Native60: DMA gathers deferred CPU writes before a new list and resumed consumer",
          "[video_core][native60_dma_gather]") {
    const bool split_enabled = GENERATE(false, true);
    CAPTURE(split_enabled);
    auto& split = VideoCommon::GpuSplit::Get();
    REQUIRE_FALSE(split.Active());
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    GatherRasterizer rasterizer;
    Tegra::MemoryManager memory{system, device_memory, 32, 1ULL << 31};
    memory.BindRasterizer(&rasterizer);
    Tegra::Control::ChannelState channel{0};
    Tegra::DmaPusher pusher{system, memory, channel};
    pusher.BindRasterizer(&rasterizer);
    ConsumerEngine consumer{rasterizer};
    pusher.BindSubchannel(&consumer, 0, Tegra::Engines::EngineTypes::Fermi2D);
    const auto old_ampere = Tegra::g_nx2_ampere.exchange(true);
    const auto old_accuracy = Settings::values.current_gpu_accuracy;
    const auto old_dma = Settings::values.dma_accuracy.GetValue();
    const auto old_backend = Settings::values.renderer_backend.GetValue();
    SCOPE_EXIT {
        split.Drain();
        split.Stop();
        Tegra::g_nx2_ampere.store(old_ampere);
        Settings::values.current_gpu_accuracy = old_accuracy;
        Settings::values.dma_accuracy.SetValue(old_dma);
        Settings::values.renderer_backend.SetValue(old_backend);
    };
    if (split_enabled) {
        Settings::values.current_gpu_accuracy = Settings::GpuAccuracy::Low;
        Settings::values.dma_accuracy.SetValue(Settings::DmaAccuracy::Unsafe);
        Settings::values.renderer_backend.SetValue(Settings::RendererBackend::Vulkan);
        // Starts only the CPU command worker; no GPU or Vulkan device is created.
        split.MaybeStart(system);
        REQUIRE(split.Active());
    }
    using Access = Tegra::Native60DmaPusherTestAccess;
    SECTION("Each new list gathers before its first executable method") {
        for (const u32 value : {11U, 22U}) {
            rasterizer.cpu_value.store(value);
            pusher.Push(List(false));
            REQUIRE(Access::Step(pusher));
            split.Drain();
            REQUIRE_FALSE(consumer.observed.empty());
            REQUIRE(consumer.observed.back() == value);
        }
        CHECK(consumer.observed == std::vector<u32>{11, 22});
        CHECK(rasterizer.gathers.load() == 2);
        CHECK_FALSE(Access::Step(pusher));
        CHECK(rasterizer.gathers.load() == 2);
        CHECK(rasterizer.back_gathers.load() == (split_enabled ? 2 : 0));
    }
    SECTION("A suspended list gathers again only after its real acquire resumes") {
        memory.Map(0x4000, 0x1000, 0x1000, Tegra::PTEKind::PITCH, false);
        device_memory.KeepWrites(0x1000, 0x1000);
        const u32 waiting = 0;
        REQUIRE(device_memory.WriteKept(0x1000, &waiting, sizeof(waiting)));
        rasterizer.cpu_value.store(11);
        pusher.Push(List(true));
        REQUIRE_FALSE(Access::Step(pusher));
        split.Drain();
        REQUIRE(pusher.HasPendingAcquire());
        REQUIRE(pusher.suspensions.load() == 1);
        REQUIRE(consumer.observed == std::vector<u32>{11});
        REQUIRE(rasterizer.gathers.load() == 1);
        REQUIRE_FALSE(Access::Step(pusher));
        CHECK(rasterizer.gathers.load() == 1);
        rasterizer.cpu_value.store(22);
        const u32 released = 7;
        REQUIRE(device_memory.WriteKept(0x1000, &released, sizeof(released)));
        REQUIRE(memory.Read<u32>(0x4000) == released);
        REQUIRE(Access::Step(pusher));
        split.Drain();
        CHECK_FALSE(pusher.HasPendingAcquire());
        CHECK(pusher.suspensions.load() == 2);
        CHECK(consumer.observed == std::vector<u32>{11, 22});
        CHECK(rasterizer.gathers.load() == 2);
        CHECK(rasterizer.back_gathers.load() == (split_enabled ? 2 : 0));
    }
}
