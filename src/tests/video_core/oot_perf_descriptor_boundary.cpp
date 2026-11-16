// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "video_core/renderer_vulkan/descriptor_frame_cursor.h"

TEST_CASE("Descriptor frame capacity is resolved before recording draw bindings",
          "[video_core][oot_perf][descriptor_boundary]") {
    Vulkan::DescriptorFrameCursor cursor;
    constexpr u64 capacity = 1024;
    constexpr size_t chunks = 2;
    for (size_t frame = 0; frame < 3; ++frame) {
        for (size_t chunk = 0; chunk < chunks; ++chunk) {
            for (u64 offset = 0; offset < capacity; offset += 256) {
                REQUIRE(cursor.Fits(256, capacity, chunks));
                const auto alloc = cursor.Allocate(256, capacity, chunks);
                REQUIRE(alloc);
                REQUIRE(alloc->chunk == chunk);
                REQUIRE(alloc->offset == offset);
            }
        }
        // The next draw's preflight must finish the previous epoch. Allocation alone cannot
        // reset into live descriptors or cause an implicit submission between bind and draw.
        REQUIRE_FALSE(cursor.Fits(1, capacity, chunks));
        REQUIRE_FALSE(cursor.Allocate(1, capacity, chunks));
        REQUIRE_FALSE(cursor.Allocate(capacity, capacity, chunks));
        cursor.Reset();
        REQUIRE(cursor.Fits(capacity, capacity, chunks));
    }
}

TEST_CASE("Descriptor frame rollover preserves exact offsets and refuses oversize sets",
          "[video_core][oot_perf][descriptor_boundary]") {
    Vulkan::DescriptorFrameCursor cursor;
    REQUIRE_FALSE(cursor.Allocate(1025, 1024, 2));
    REQUIRE_FALSE(cursor.Allocate(16, 1024, 0));
    REQUIRE(cursor.Allocate(768, 1024, 2)->offset == 0);
    const auto next = cursor.Allocate(512, 1024, 2);
    REQUIRE(next);
    REQUIRE(next->chunk == 1);
    REQUIRE(next->offset == 0);
    REQUIRE(cursor.Allocate(512, 1024, 2)->offset == 512);
    REQUIRE_FALSE(cursor.Fits(256, 1024, 2));
}
