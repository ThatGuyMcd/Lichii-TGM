// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "audio_core/sink/sysmix_queue.h"

TEST_CASE("Sysmix queue covers the host callback without overflowing its sample ring",
          "[sysmix_sink][sysmix_queue]") {
    using AudioCore::Sink::SysmixQueueBlocks;
    constexpr u64 Capacity = 0x10000;
    CHECK(SysmixQueueBlocks(0, 2, Capacity) == 3);
    CHECK(SysmixQueueBlocks(240, 2, Capacity) == 3);
    CHECK(SysmixQueueBlocks(480, 2, Capacity) == 3);
    CHECK(SysmixQueueBlocks(481, 2, Capacity) == 4);
    CHECK(SysmixQueueBlocks(1024, 2, Capacity) == 6);
    CHECK(SysmixQueueBlocks(1024, 6, Capacity) == 6);
    CHECK(SysmixQueueBlocks(4096, 2, Capacity) == 19);
    CHECK(SysmixQueueBlocks((std::numeric_limits<u64>::max)(), 2, Capacity) == 64);
    CHECK(SysmixQueueBlocks((std::numeric_limits<u64>::max)(), 6, Capacity) == 44);
    for (const u32 channels : {2U, 6U}) {
        for (const u64 frames : std::array<u64, 5>{0, 481, 1024, 4096,
                                                  (std::numeric_limits<u64>::max)()}) {
            CAPTURE(channels, frames);
            const u64 queued = SysmixQueueBlocks(frames, channels, Capacity);
            CHECK((queued + 1) * AudioCore::TargetSampleCount * channels <= Capacity);
        }
    }
    CHECK(SysmixQueueBlocks(1024, 0, Capacity) == 0);
    CHECK(SysmixQueueBlocks(1024, 6, 0) == 0);
    CHECK(SysmixQueueBlocks(1024, 6, 1440) == 0);
    CHECK(SysmixQueueBlocks(1024, 6, 2880) == 1);
}
