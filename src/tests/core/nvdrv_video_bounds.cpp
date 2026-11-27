// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <bit>
#include <limits>
#include <span>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "core/hle/service/nvdrv/devices/nvdec_submit.h"
#include "core/hle/service/nvnflinger/ui/fence.h"
#include "video_core/host1x/codecs/h264_surface_offsets.h"
#include "video_core/host1x/host1x.h"

using Service::Nvidia::MaxSyncPoints;
using Service::Nvidia::Devices::ApplyNvdecSyncpointIncrements;
using Service::Nvidia::Devices::NvdecSyncpointIncrement;

TEST_CASE("Composite sees only the acquire fences stored in a layer", "[nvdrv_video_bounds]") {
    auto fence = Service::android::Fence::NoFence();
    fence.fences[0] = {7, 11};
    fence.fences[2] = {13, 29};
    CHECK(fence.ActiveFences().empty());

    fence.num_fences = 2;
    REQUIRE(fence.ActiveFences().size() == 2);
    CHECK(fence.ActiveFences()[0].id == 7);
    CHECK(fence.ActiveFences()[1].id == -1);

    fence.num_fences = static_cast<u32>(fence.fences.size());
    REQUIRE(fence.ActiveFences().size() == fence.fences.size());
    CHECK(fence.ActiveFences()[2].value == 29);

    fence.num_fences = std::numeric_limits<u32>::max();
    REQUIRE(fence.ActiveFences().size() == fence.fences.size());
    CHECK(fence.ActiveFences().back().id == -1);
}

TEST_CASE("NVDEC advances every valid syncpoint and writes only available thresholds",
          "[nvdrv_video_bounds]") {
    constexpr std::array increments{
        NvdecSyncpointIncrement{.id = 3, .increments = 4},
        NvdecSyncpointIncrement{.id = MaxSyncPoints - 1, .increments = 9},
        NvdecSyncpointIncrement{.id = 3, .increments = 6},
    };
    std::array<u32, MaxSyncPoints> maxima{};
    maxima[3] = 120;
    maxima.back() = 200;
    std::array<u32, 4> words{31, 37, 41, 43};
    const auto advance = [&](u32 id, u32 count) { return maxima.at(id) += count; };

    REQUIRE_FALSE(ApplyNvdecSyncpointIncrements(increments, std::span{words}.subspan(1, 1), advance));
    CHECK((words == std::array<u32, 4>{31, 124, 41, 43}));
    CHECK(maxima[3] == 130);
    CHECK(maxima.back() == 209);

    REQUIRE_FALSE(ApplyNvdecSyncpointIncrements(increments, {}, advance));
    CHECK(maxima[3] == 140);
    CHECK(maxima.back() == 218);

    REQUIRE_FALSE(ApplyNvdecSyncpointIncrements(increments, words, advance));
    CHECK((words == std::array<u32, 4>{144, 227, 150, 43}));
}

TEST_CASE("NVDEC rejects a later out-of-range syncpoint before advancing the first",
          "[nvdrv_video_bounds]") {
    std::array increments{
        NvdecSyncpointIncrement{.id = 9, .increments = 5},
        NvdecSyncpointIncrement{.id = MaxSyncPoints, .increments = 12},
    };
    std::array<u32, 2> thresholds{27, 32};
    u32 advance_count{};
    const auto advance = [&](u32, u32) { return ++advance_count; };
    CHECK(ApplyNvdecSyncpointIncrements(increments, thresholds, advance) == MaxSyncPoints);
    CHECK(advance_count == 0);
    CHECK((thresholds == std::array<u32, 2>{27, 32}));

    increments[1].id = std::numeric_limits<u32>::max();
    CHECK(ApplyNvdecSyncpointIncrements(increments, {}, advance) == increments[1].id);
    CHECK(advance_count == 0);
}

TEST_CASE("Host1x only indexes the device slots it owns", "[nvdrv_video_bounds]") {
    using Host = Tegra::Host1x::Host1x;
    CHECK_FALSE(Host::ValidFd(std::numeric_limits<s32>::min()));
    CHECK_FALSE(Host::ValidFd(-1));
    CHECK(Host::ValidFd(0));
    CHECK(Host::ValidFd(static_cast<s32>(Host::DeviceCapacity - 1)));
    CHECK_FALSE(Host::ValidFd(static_cast<s32>(Host::DeviceCapacity)));
    CHECK_FALSE(Host::ValidFd(std::numeric_limits<s32>::max()));
}

TEST_CASE("H264 picture surfaces use both valid planes and reject out-of-range indices",
          "[nvdrv_video_bounds]") {
    using Tegra::Host1x::NvdecCommon::NvdecRegisters;
    using Tegra::Host1x::NvdecCommon::Offset;
    using Tegra::Decoders::FindH264SurfaceOffsets;

    NvdecRegisters regs{};
    regs.surface_luma_offsets.front() = std::bit_cast<Offset>(u64{0x32});
    regs.surface_chroma_offsets.front() = std::bit_cast<Offset>(u64{0x51});
    regs.surface_luma_offsets.back() = std::bit_cast<Offset>(u64{0x7a});
    regs.surface_chroma_offsets.back() = std::bit_cast<Offset>(u64{0x92});
    const u32 count = static_cast<u32>(regs.surface_luma_offsets.size());

    CHECK((FindH264SurfaceOffsets(regs, 0) == std::pair<u64, u64>{0x3200, 0x5100}));
    CHECK((FindH264SurfaceOffsets(regs, count - 1) == std::pair<u64, u64>{0x7a00, 0x9200}));
    CHECK_FALSE(FindH264SurfaceOffsets(regs, count));
    CHECK_FALSE(FindH264SurfaceOffsets(regs, std::numeric_limits<u32>::max()));
}
