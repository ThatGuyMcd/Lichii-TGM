// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "video_core/renderer_vulkan/stream_buffer_cursor.h"

TEST_CASE("Refused Vulkan stream wraps cannot grant an in-flight first region",
          "[video_core][oot_perf][stream_wrap]") {
    Vulkan::StreamBufferCursor cursor;
    constexpr size_t capacity = 64 * 1024;
    constexpr size_t region = capacity / Vulkan::StreamBufferCursor::NumRegions;
    // Fill all but the last region while the first upload's tick remains incomplete.
    for (size_t i = 0; i < Vulkan::StreamBufferCursor::NumRegions - 1; ++i) {
        REQUIRE(cursor.Reserve(region, capacity, 7, 0) == i * region);
    }
    // A large request cannot wrap. Afterwards small requests must either use the untouched
    // tail or use dedicated staging, never overwrite the first upload at offset zero.
    REQUIRE_FALSE(cursor.Reserve(region, capacity, 8, 0));
    for (size_t i = 0; i < region / 256 + 2; ++i) {
        const auto offset = cursor.Reserve(256, capacity, 8, 0);
        if (offset) {
            REQUIRE(*offset >= capacity - region);
            REQUIRE(*offset + 256 <= capacity);
        }
    }
    REQUIRE(cursor.Reserve(region, capacity, 9, 8) == 0);
    REQUIRE(cursor.Reserve(256, capacity, 9, 8) == region);
}

TEST_CASE("Vulkan stream rejected wrap preserves region-size boundary state",
          "[video_core][oot_perf][stream_wrap]") {
    Vulkan::StreamBufferCursor cursor;
    constexpr size_t capacity = 64 * 1024;
    constexpr size_t region = capacity / Vulkan::StreamBufferCursor::NumRegions;
    const size_t request = GENERATE_COPY(size_t{256}, region);
    for (size_t i = 0; i < capacity / 256 - 1; ++i) {
        REQUIRE(cursor.Reserve(256, capacity, 11, 0) == i * 256);
    }
    REQUIRE_FALSE(cursor.Reserve(request, capacity, 12, 0));
    REQUIRE_FALSE(cursor.Reserve(256, capacity, 12, 0));
    REQUIRE_FALSE(cursor.Reserve(256, capacity, 12, 10));
    REQUIRE(cursor.Reserve(256, capacity, 13, 12) == 0);
}

TEST_CASE("Vulkan stream budget tails cannot form an untracked region",
          "[video_core][oot_perf][stream_wrap]") {
    Vulkan::StreamBufferCursor cursor;
    constexpr size_t capacity = 64 * 1024 + 256;
    for (size_t i = 0; i < 255; ++i) {
        REQUIRE(cursor.Reserve(256, capacity, 1, 0) == i * 256);
    }
    REQUIRE_FALSE(cursor.Reserve(256, capacity, 1, 0));
    REQUIRE_FALSE(cursor.Reserve(256, capacity, 1, 0));
    REQUIRE(cursor.Reserve(256, capacity, 2, 1) == 0);
    REQUIRE_FALSE(cursor.Reserve(0, capacity, 2, 1));
}
