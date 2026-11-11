// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/precache_thread_policy.h"

namespace {
constexpr u64 GiB = 1ULL << 30;
} // namespace

TEST_CASE("Automatic precache workers handle exhausted and unknown commit limits",
          "[video_core][precache]") {
    using Vulkan::SelectPrecacheThreads;
    CHECK(SelectPrecacheThreads(16, 0, 0, 8 * GiB, 8 * GiB) == 1);
    CHECK(SelectPrecacheThreads(16, 0, 0, 9 * GiB, 8 * GiB) == 1);
    CHECK(SelectPrecacheThreads(16, 0, 0, std::numeric_limits<u64>::max(), 8 * GiB) == 1);
    CHECK(SelectPrecacheThreads(16, 0, 0, 8 * GiB, 0) == 16);
    CHECK(SelectPrecacheThreads(0, 0, 0, 0, 0) == 1);
    CHECK(SelectPrecacheThreads(128, 0, 0, 0, 0) == 64);
}

TEST_CASE("Automatic precache workers reserve commit headroom without exceeding CPU count",
          "[video_core][precache]") {
    using Vulkan::SelectPrecacheThreads;
    CHECK(SelectPrecacheThreads(16, 0, 0, 7 * GiB, 8 * GiB) == 1);
    CHECK(SelectPrecacheThreads(16, 0, 0, 6 * GiB, 8 * GiB) == 1);
    CHECK(SelectPrecacheThreads(16, 0, 0, 2 * GiB, 8 * GiB) == 2);
    CHECK(SelectPrecacheThreads(2, 0, 0, 2 * GiB, 16 * GiB) == 2);
}

TEST_CASE("Explicit precache worker counts retain priority over automatic memory limits",
          "[video_core][precache]") {
    using Vulkan::SelectPrecacheThreads;
    CHECK(SelectPrecacheThreads(16, 8, 0, 8 * GiB, 8 * GiB) == 8);
    CHECK(SelectPrecacheThreads(16, 8, 3, 8 * GiB, 8 * GiB) == 3);
    CHECK(SelectPrecacheThreads(16, 0, 64, 9 * GiB, 8 * GiB) == 64);
    CHECK(SelectPrecacheThreads(16, 100, 0, 8 * GiB, 8 * GiB) == 64);
}
