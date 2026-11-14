// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/memory_detect.h"
#include "video_core/texture_cache/host_memory_limit.h"

TEST_CASE("Sparse host spills cannot bunch forced collections after a quiet gap",
          "[video_core][texture_cache][host_memory_limit][host_spill_spacing]") {
    VideoCommon::HostSpillCollections collections;
    std::vector<u64> collected;
    for (const u64 frame : {1ULL, 100ULL, 101ULL, 102ULL, 114ULL, 115ULL, 116ULL,
                            129ULL, 130ULL, 145ULL, 1000ULL}) {
        if (collections.TryCollect(frame)) {
            collected.push_back(frame);
        }
    }
    REQUIRE(collected == std::vector<u64>{1, 100, 115, 130});
    REQUIRE(collections.Count() == 4);

    // TextureCache resets the episode after 300 quiet frames, including its four-collect cap.
    collections.Reset();
    REQUIRE(collections.Count() == 0);
    REQUIRE(collections.TryCollect(1001));
    REQUIRE_FALSE(collections.TryCollect(1015));
    REQUIRE(collections.TryCollect(1016));
}

TEST_CASE("Continuous host spills keep the existing four-collect budget",
          "[video_core][texture_cache][host_memory_limit][host_spill_spacing]") {
    VideoCommon::HostSpillCollections collections;
    std::vector<u64> collected;
    for (u64 frame = 0; frame < 120; ++frame) {
        if (collections.TryCollect(frame)) {
            collected.push_back(frame);
        }
    }
    REQUIRE(collected == std::vector<u64>{0, 15, 30, 45});
    REQUIRE(collections.Count() == 4);
}

TEST_CASE("Native texture ceilings change continuously with the device budget",
          "[video_core][texture_cache][host_memory_limit]") {
    using VideoCommon::TextureGcDeviceCeiling;
    constexpr u64 GiB = u64{1} << 30;
    CHECK(TextureGcDeviceCeiling(8 * GiB, 0, true) == 5 * GiB);
    CHECK(TextureGcDeviceCeiling(9 * GiB, 0, true) == 25 * GiB / 4);
    CHECK(TextureGcDeviceCeiling(10 * GiB, 0, true) == 15 * GiB / 2);
    const u64 before = TextureGcDeviceCeiling(10 * GiB - 1, 0, true);
    const u64 after = TextureGcDeviceCeiling(10 * GiB, 0, true);
    CHECK(after - before <= 2);
    CHECK(TextureGcDeviceCeiling(9 * GiB, 0, false) == 45 * GiB / 8);
    constexpr u64 max = (std::numeric_limits<u64>::max)();
    CHECK(TextureGcDeviceCeiling(max, 0, true) == max - 5 * GiB / 2);
    u64 critical = TextureGcDeviceCeiling(12 * GiB, 0, true);
    u64 expected = critical - GiB;
    u64 minimum = expected - GiB;
    VideoCommon::ClampToHostCriticalMemory(4 * GiB, GiB, expected, critical, minimum);
    CHECK(critical == 4 * GiB); // The larger device ceiling still obeys the final host clamp.
}

namespace {

constexpr u64 MiB = u64{1} << 20;
constexpr u64 GiB = u64{1} << 30;

} // namespace

TEST_CASE("Host memory with room to spare does not bound the texture cache",
          "[video_core][texture_cache][host_memory_limit]") {
    // 32 GiB of RAM and a 16 GB card: far above the 9.7 GiB its device budget allows.
    REQUIRE(VideoCommon::HostCriticalMemory(6 * GiB, 20 * GiB) == 24 * GiB);
    REQUIRE(VideoCommon::HostCriticalMemory(6 * GiB, 20 * GiB) > 9723 * MiB);
}

TEST_CASE("The texture cache collects before the host runs out of commit",
          "[video_core][texture_cache][host_memory_limit]") {
    // The RX 7600 XT session: thresholds at 8955 / 9723 MiB from the device budget alone, on
    // a host with a 31.9 GiB commit limit that the guest's memory takes 14 GiB of.
    const u64 critical = VideoCommon::HostCriticalMemory(7 * GiB, 1 * GiB);
    REQUIRE(critical == 6 * GiB);
    REQUIRE(critical < 7 * GiB);
    // Freeing device memory gives the commit back, which leaves the threshold where it was.
    REQUIRE(VideoCommon::HostCriticalMemory(5 * GiB, 3 * GiB) == critical);
    // Commit taken by something else lowers it.
    REQUIRE(VideoCommon::HostCriticalMemory(7 * GiB, 512 * MiB) < critical);
}

TEST_CASE("The texture cache keeps what a scene needs when the host is out of commit",
          "[video_core][texture_cache][host_memory_limit]") {
    REQUIRE(VideoCommon::HostCriticalMemory(4 * GiB, 0) == VideoCommon::MinHostCriticalMemory);
    REQUIRE(VideoCommon::HostCriticalMemory(0, 0) == VideoCommon::MinHostCriticalMemory);
    REQUIRE(VideoCommon::HostCriticalMemory(1 * GiB, 1 * GiB) ==
            VideoCommon::MinHostCriticalMemory);
}

TEST_CASE("The host memory bound does not wrap", "[video_core][texture_cache][host_memory_limit]") {
    constexpr u64 max = (std::numeric_limits<u64>::max)();
    REQUIRE(VideoCommon::HostCriticalMemory(8 * GiB, max) == max - VideoCommon::HostCommitReserve);
    REQUIRE(VideoCommon::HostCriticalMemory(max, max) == max - VideoCommon::HostCommitReserve);
}

TEST_CASE("The system's commit room is reported where there is a commit limit",
          "[video_core][texture_cache][host_memory_limit]") {
    const std::optional<u64> room = Common::GetSystemCommitRoom();
#ifdef _WIN32
    REQUIRE(room.has_value());
    const Common::ProcessMemoryUsage usage = Common::GetProcessMemoryUsage();
    REQUIRE(*room <= usage.system_commit_limit);
#else
    REQUIRE_FALSE(room.has_value());
#endif
}

TEST_CASE("Host commit bounds the integrated GPU adjustment and keeps smaller overrides",
          "[video_core][texture_cache][host_memory_limit]") {
    u64 critical = 10 * GiB;
    u64 expected = 9 * GiB;
    u64 minimum = 2 * GiB;
    // The heap adjustment allowed more cache, but system commit only leaves this much room.
    const u64 host_limit = VideoCommon::HostCriticalMemory(4 * GiB, 1 * GiB);
    VideoCommon::ClampToHostCriticalMemory(host_limit, GiB, expected, critical, minimum);
    REQUIRE(critical == 3 * GiB);
    REQUIRE(expected == 2 * GiB);
    REQUIRE(minimum == GiB);

    // An explicit smaller expected threshold is retained while the critical one is bounded.
    critical = 10 * GiB;
    expected = 512 * MiB;
    VideoCommon::ClampToHostCriticalMemory(host_limit, GiB, expected, critical, minimum);
    REQUIRE(critical == host_limit);
    REQUIRE(expected == 512 * MiB);
    REQUIRE(minimum == 0);
}

TEST_CASE("An unknown host commit limit leaves the existing device thresholds alone",
          "[video_core][texture_cache][host_memory_limit]") {
    u64 critical = 10 * GiB;
    u64 expected = 9 * GiB;
    u64 minimum = 2 * GiB;
    VideoCommon::ClampToHostCriticalMemory((std::numeric_limits<u64>::max)(), GiB,
                                          expected, critical, minimum);
    REQUIRE(critical == 10 * GiB);
    REQUIRE(expected == 9 * GiB);
    REQUIRE(minimum == 2 * GiB);
}
