// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "common/scope_exit.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "tests/video_core/native60_dma_test_access.h"
#include "video_core/control/channel_state.h"
#include "video_core/dma_pusher.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/gpu_split.h"
#include "video_core/macro.h"
#include "video_core/memory_manager.h"
#include "video_core/nx2_fast_draw.h"


namespace {
class SerialEngine final : public Tegra::Engines::EngineInterface {
public:
    std::vector<std::pair<u32, u32>> received;
    std::vector<u64> serials;

    void CallMethod(Core::System&, u32 method, u32 value, bool) override {
        received.emplace_back(method, value);
        serials.push_back(VideoCommon::Nx2FastDraw::Serial());
    }
    void CallMultiMethod(Core::System& system, u32 method, const u32* words, u32 amount,
                         u32) override {
        for (u32 i = 0; i < amount; ++i) {
            CallMethod(system, method, words[i], true);
        }
    }
};

} // namespace
TEST_CASE("Native60: Unsplit command parser shadows copy and upload registers it batches",
          "[video_core][command_writer]") {
    // A split started later places copy launches and inline uploads from these shadows
    // (DmaPusher::NoteEngineWrites): registers set before it must be there too.
    REQUIRE_FALSE(VideoCommon::GpuSplit::Get().Active());
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    Tegra::MemoryManager memory{system, device_memory, 32, 1ULL << 31};
    Tegra::Control::ChannelState channel{0};
    Tegra::DmaPusher pusher{system, memory, channel};
    SerialEngine copy;
    SerialEngine upload;
    copy.reports_fast_draw_breaks = true;
    upload.reports_fast_draw_breaks = true;
    pusher.BindSubchannel(&copy, 0, Tegra::Engines::EngineTypes::MaxwellDMA);
    pusher.BindSubchannel(&upload, 1, Tegra::Engines::EngineTypes::KeplerMemory);
    const auto header = [](u32 subchannel, u32 method, u32 count) {
        auto result = Tegra::BuildCommandHeader(static_cast<Tegra::BufferMethods>(method), count,
                                                Tegra::SubmissionMode::Increasing);
        result.subchannel.Assign(subchannel);
        return result;
    };
    const std::array words{header(0, 0x100, 3),
                           Tegra::CommandHeader{.argument = 0x11},
                           Tegra::CommandHeader{.argument = 0x22},
                           Tegra::CommandHeader{.argument = 0x33},
                           header(1, 0x60, 2),
                           Tegra::CommandHeader{.argument = 0x44},
                           Tegra::CommandHeader{.argument = 0x55}};
    using Access = Tegra::Native60DmaPusherTestAccess;
    REQUIRE(Access::Process(pusher, words) == words.size());
    // Batched into the engines' sinks, past CallMethod.
    REQUIRE(copy.received.empty());
    REQUIRE(copy.method_sink.size() == 3);
    REQUIRE(upload.method_sink.size() == 2);
    REQUIRE(Access::DmaRegister(pusher, 0x100) == 0x11);
    REQUIRE(Access::DmaRegister(pusher, 0x101) == 0x22);
    REQUIRE(Access::DmaRegister(pusher, 0x102) == 0x33);
    REQUIRE(Access::InlineToMemoryRegister(pusher, 0x60) == 0x44);
    REQUIRE(Access::InlineToMemoryRegister(pusher, 0x61) == 0x55);
}
