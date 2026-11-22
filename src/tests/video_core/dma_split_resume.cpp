// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Independent synthetic Step-level regressions for Lichii #2415 / Eglantine #924.

#include <array>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "tests/video_core/native60_dma_test_access.h"
#include "tests/video_core/oot_perf_null_rasterizer.h"
#include "video_core/control/channel_state.h"
#include "video_core/gpu_split.h"
#include "video_core/memory_manager.h"
#include "video_core/nx2.h"

namespace {
using Header = Tegra::CommandHeader;
using Mode = Tegra::SubmissionMode;
using Access = Tegra::Native60DmaPusherTestAccess;
using Writes = std::vector<std::pair<u32, u32>>;

Header Write(u32 method, u32 count, Mode mode = Mode::Increasing) {
    return Tegra::BuildCommandHeader(static_cast<Tegra::BufferMethods>(method), count, mode);
}
Header Data(u32 value) { return {.argument = value}; }

struct Entry {
    GPUVAddr address;
    std::vector<Header> words;
};
Tegra::CommandList List(std::initializer_list<Entry> entries) {
    Tegra::CommandList result{entries.size()};
    size_t i{};
    for (const auto& entry : entries) {
        result.command_lists[i].addr.Assign(entry.address);
        result.command_lists[i].size.Assign(static_cast<u32>(entry.words.size()));
        result.submitted_offsets.push_back(static_cast<u32>(result.submitted_words.size()));
        result.submitted_words.insert(result.submitted_words.end(), entry.words.begin(), entry.words.end());
        ++i;
    }
    return result;
}

class RecordingEngine final : public Tegra::Engines::EngineInterface {
public:
    Writes received;
    RecordingEngine() { execution_mask.set(); }
    void CallMethod(Core::System&, u32 method, u32 value, bool) override {
        received.emplace_back(method, value);
    }
    void CallMultiMethod(Core::System& system, u32 method, const u32* values, u32 count, u32) override {
        for (u32 i = 0; i < count; ++i) CallMethod(system, method, values[i], true);
    }
};

class CopyRasterizer final : public OotPerfNullRasterizer {
public:
    bool ready{};
    u32 reads{}, tick_checks{};
    DAddr copy_address{};
    std::vector<Header> copied;
    GpuWrittenList ReadGpuWrittenCommandList(DAddr address, std::span<u8> out, bool,
                                            u64* pending_tick) override {
        ++reads;
        REQUIRE(address == copy_address);
        REQUIRE(out.size() == copied.size() * sizeof(Header));
        if (!ready) {
            REQUIRE(pending_tick != nullptr);
            *pending_tick = 73;
            return GpuWrittenList::Pending;
        }
        std::memcpy(out.data(), copied.data(), out.size());
        return GpuWrittenList::Copied;
    }
    bool IsGpuTickDone(u64 tick) override {
        ++tick_checks;
        REQUIRE(tick == 73);
        return ready;
    }
};

struct Fixture {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    CopyRasterizer rasterizer;
    Tegra::MemoryManager memory{system, device_memory, 32, 1ULL << 31};
    Tegra::Control::ChannelState channel{0};
    Tegra::DmaPusher pusher{system, memory, channel};
    RecordingEngine engine;
    const bool old_ampere = Tegra::g_nx2_ampere.load();
    const auto old_accuracy = Settings::values.current_gpu_accuracy;
    const auto old_dma = Settings::values.dma_accuracy.GetValue();
    const auto old_backend = Settings::values.renderer_backend.GetValue();

    explicit Fixture(bool split = false, bool native = true) {
        memory.BindRasterizer(&rasterizer);
        pusher.BindRasterizer(&rasterizer);
        pusher.BindSubchannel(&engine, 0, Tegra::Engines::EngineTypes::Fermi2D);
        Tegra::g_nx2_ampere.store(native);
        Settings::values.current_gpu_accuracy = Settings::GpuAccuracy::Low;
        Settings::values.dma_accuracy.SetValue(Settings::DmaAccuracy::Unsafe);
        Settings::values.renderer_backend.SetValue(Settings::RendererBackend::Vulkan);
        // This starts only the synthetic command worker, never a Vulkan device or emulator.
        if (split) VideoCommon::GpuSplit::Get().MaybeStart(system);
    }
    ~Fixture() {
        auto& split = VideoCommon::GpuSplit::Get();
        split.Drain();
        split.Stop();
        Tegra::g_nx2_ampere.store(old_ampere);
        Settings::values.current_gpu_accuracy = old_accuracy;
        Settings::values.dma_accuracy.SetValue(old_dma);
        Settings::values.renderer_backend.SetValue(old_backend);
    }
    void Steps(size_t count) {
        for (size_t i = 0; i < count; ++i) REQUIRE(Access::Step(pusher));
        VideoCommon::GpuSplit::Get().Drain();
    }
};
}

TEST_CASE("Split register data resumes after an intervening call on either command worker mode",
          "[video_core][dma_split_resume]") {
    const bool active = GENERATE(false, true);
    const auto mode = GENERATE(Mode::Increasing, Mode::NonIncreasing, Mode::IncreaseOnce);
    REQUIRE_FALSE(VideoCommon::GpuSplit::Get().Active());
    Fixture f(active);
    REQUIRE(VideoCommon::GpuSplit::Get().Active() == active);
    f.pusher.Push(List({{0x1000, {Write(0x80, 4, mode), Data(11)}},
                        {0x2000, {Write(0x90, 31, Mode::Inline), Write(0x91, 32, Mode::Inline)}},
                        {0x1008, {Data(12), Data(13), Data(14)}}}));
    f.Steps(3);
    const u32 second = mode == Mode::NonIncreasing ? 0x80 : 0x81;
    const u32 third = mode == Mode::Increasing ? 0x82 : second;
    const u32 fourth = mode == Mode::Increasing ? 0x83 : second;
    REQUIRE(f.engine.received == Writes{{0x80, 11}, {0x90, 31}, {0x91, 32},
                                        {second, 12}, {third, 13}, {fourth, 14}});
    REQUIRE(f.pusher.parked_methods.empty());
    REQUIRE(f.pusher.dma_state.method_count == 0);
}

TEST_CASE("Returning to a parked write preserves the other active register write",
          "[video_core][dma_split_resume]") {
    REQUIRE_FALSE(VideoCommon::GpuSplit::Get().Active());
    Fixture f;
    f.pusher.Push(List({{0x1000, {Write(0x80, 2)}},
                        {0x2000, {Write(0x90, 4), Data(21), Data(22)}},
                        {0x1004, {Data(11), Data(12)}},
                        {0x200c, {Data(23), Data(24)}}}));
    f.Steps(4);
    REQUIRE(f.engine.received == Writes{{0x90, 21}, {0x91, 22}, {0x80, 11},
                                        {0x81, 12}, {0x92, 23}, {0x93, 24}});
    REQUIRE(f.pusher.parked_methods.empty());
}

TEST_CASE("Macro parameters remain data across noncontiguous differently sized entries",
          "[video_core][dma_split_resume]") {
    const bool active = GENERATE(false, true);
    REQUIRE_FALSE(VideoCommon::GpuSplit::Get().Active());
    Fixture f(active);
    REQUIRE(VideoCommon::GpuSplit::Get().Active() == active);
    // Later return to the original end would fool the register-call heuristic without
    // its macro guard. This is independently constructed payload, not a game capture.
    f.pusher.Push(List({{0x1000, {Write(0xe20, 7, Mode::NonIncreasing), Data(11)}},
                        {0x2000, {Data(12), Data(13), Data(14)}},
                        {0x1008, {Data(15), Data(16), Data(17)}}}));
    f.Steps(3);
    REQUIRE(f.engine.received == Writes{{0xe20, 11}, {0xe20, 12}, {0xe20, 13}, {0xe20, 14},
                                        {0xe20, 15}, {0xe20, 16}, {0xe20, 17}});
    REQUIRE(f.pusher.parked_methods.empty());
    REQUIRE(f.pusher.dma_state.method_count == 0);
}

TEST_CASE("A noncontiguous register payload without a later continuation remains data",
          "[video_core][dma_split_resume]") {
    REQUIRE_FALSE(VideoCommon::GpuSplit::Get().Active());
    Fixture f;
    f.pusher.Push(List({{0x1000, {Write(0x80, 4), Data(11)}},
                        {0x2000, {Data(12), Data(13)}}, {0x3000, {Data(14)}}}));
    f.Steps(3);
    REQUIRE(f.engine.received == Writes{{0x80, 11}, {0x81, 12}, {0x82, 13}, {0x83, 14}});
    REQUIRE(f.pusher.parked_methods.empty());
}

TEST_CASE("An exact-sized noncontiguous payload is not classified as a call",
          "[video_core][dma_split_resume]") {
    REQUIRE_FALSE(VideoCommon::GpuSplit::Get().Active());
    Fixture f;
    f.pusher.Push(List({{0x1000, {Write(0x80, 3), Data(11)}},
                        {0x2000, {Data(12), Data(13)}},
                        {0x1008, {Write(0x90, 31, Mode::Inline)}}}));
    f.Steps(3);
    REQUIRE(f.engine.received == Writes{{0x80, 11}, {0x81, 12}, {0x82, 13}, {0x90, 31}});
}

TEST_CASE("An empty continuation entry does not make another entry a call",
          "[video_core][dma_split_resume]") {
    REQUIRE_FALSE(VideoCommon::GpuSplit::Get().Active());
    Fixture f;
    f.pusher.Push(List({{0x1000, {Write(0x80, 4), Data(11)}},
                        {0x2000, {Data(12), Data(13)}}, {0x1008, {}},
                        {0x3000, {Data(14)}}}));
    f.Steps(4);
    REQUIRE(f.engine.received == Writes{{0x80, 11}, {0x81, 12}, {0x82, 13}, {0x83, 14}});
}

TEST_CASE("Ordinary Maxwell streams do not use native split-call inference",
          "[video_core][dma_split_resume]") {
    REQUIRE_FALSE(VideoCommon::GpuSplit::Get().Active());
    Fixture f(false, false);
    f.pusher.Push(List({{0x1000, {Write(0x80, 4), Data(11)}},
                        {0x2000, {Data(12), Data(13)}}, {0x1008, {Data(14)}}}));
    f.Steps(3);
    REQUIRE(f.engine.received == Writes{{0x80, 11}, {0x81, 12}, {0x82, 13}, {0x83, 14}});
}

TEST_CASE("Resumed GPU command copy retries keep the expected address and use fresh words",
          "[video_core][dma_split_resume]") {
    const bool active = GENERATE(false, true);
    REQUIRE_FALSE(VideoCommon::GpuSplit::Get().Active());
    Fixture f(active);
    REQUIRE(VideoCommon::GpuSplit::Get().Active() == active);
    constexpr DAddr device_page = 0x730000;
    constexpr size_t word_index = (device_page >> 12) / 64;
    const auto old_written = Tegra::g_nx2_gpu_written_pages[word_index].load();
    const auto old_command = Tegra::g_nx2_command_list_pages[word_index].load();
    SCOPE_EXIT {
        Tegra::g_nx2_gpu_written_pages[word_index].store(old_written);
        Tegra::g_nx2_command_list_pages[word_index].store(old_command);
    };
    Tegra::g_nx2_gpu_written_pages[word_index].fetch_and(~(u64{1} << ((device_page >> 12) % 64)));
    f.memory.Map(0x1000, device_page, 0x1000, Tegra::PTEKind::PITCH, false);
    f.device_memory.KeepWrites(device_page, 0x1000);
    const std::array<u32, 4> empty{};
    REQUIRE(f.device_memory.WriteKept(device_page, empty.data(), sizeof(empty)));
    f.pusher.Push(List({{0x1000, {Write(0x80, 4), Data(11), Data(12)}},
                        {0x2000, {Write(0x90, 4), Data(21), Data(22)}},
                        {0x100c, {Data(99), Data(98), Write(0xa0, 97, Mode::Inline)}},
                        {0x200c, {Data(23), Data(24)}}}));
    f.Steps(2);
    Tegra::Nx2MarkGpuWritten(device_page, 0x1000);
    f.rasterizer.copy_address = device_page + 12;
    f.rasterizer.copied = {Data(13), Data(14), Write(0xa0, 77, Mode::Inline)};
    REQUIRE_FALSE(Access::Step(f.pusher));
    REQUIRE(f.pusher.HasPendingAcquire());
    REQUIRE(f.pusher.split_method_end == 0x100c);
    REQUIRE(f.pusher.dma_state.method == 0x82);
    REQUIRE(f.pusher.dma_state.method_count == 2);
    REQUIRE(f.pusher.parked_methods.size() == 1);
    REQUIRE(f.pusher.parked_methods.front().resume_address == 0x200c);
    REQUIRE_FALSE(Access::Step(f.pusher));
    REQUIRE(f.rasterizer.reads == 1);
    f.rasterizer.ready = true;
    f.Steps(2);
    REQUIRE(f.engine.received == Writes{{0x80, 11}, {0x81, 12}, {0x90, 21}, {0x91, 22},
                                        {0x82, 13}, {0x83, 14}, {0xa0, 77}, {0x92, 23}, {0x93, 24}});
    REQUIRE(f.rasterizer.reads == 2);
    REQUIRE(f.rasterizer.tick_checks == 2);
    REQUIRE_FALSE(f.pusher.HasPendingAcquire());
    REQUIRE(f.pusher.parked_methods.empty());
    REQUIRE(f.pusher.dma_state.method_count == 0);
}

TEST_CASE("An active macro consumes a matching parked address without reviving it later",
          "[video_core][dma_split_resume]") {
    REQUIRE_FALSE(VideoCommon::GpuSplit::Get().Active());
    Fixture f;
    f.pusher.Push(List({{0x1000, {Write(0x80, 2)}},
                        {0x2000, {Write(0xe20, 4, Mode::NonIncreasing), Data(21), Data(22)}},
                        {0x1004, {Data(31), Data(32)}},
                        {0x1004, {Write(0xa0, 77, Mode::Inline)}}}));
    f.Steps(4);
    REQUIRE(f.engine.received == Writes{{0xe20, 21}, {0xe20, 22}, {0xe20, 31}, {0xe20, 32},
                                        {0xa0, 77}});
    REQUIRE(f.pusher.parked_methods.empty());
    REQUIRE(f.pusher.dma_state.method_count == 0);
}

TEST_CASE("Explicit split parking diagnostic still overrides the native macro inference",
          "[.][video_core][dma_split_override]") {
    REQUIRE(std::getenv("EDEN_NX2_PARK_SPLIT_METHODS") != nullptr);
    REQUIRE_FALSE(VideoCommon::GpuSplit::Get().Active());
    Fixture f;
    f.pusher.Push(List({{0x1000, {Write(0xe20, 3, Mode::NonIncreasing), Data(11)}},
                        {0x2000, {Write(0x90, 31, Mode::Inline)}},
                        {0x1008, {Data(12), Data(13)}}}));
    f.Steps(3);
    REQUIRE(f.engine.received == Writes{{0xe20, 11}, {0x90, 31}, {0xe20, 12}, {0xe20, 13}});
    REQUIRE(f.pusher.parked_methods.empty());
}
