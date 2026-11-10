// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>

#include "audio_core/common/common.h"

namespace AudioCore::Sink {

constexpr u32 SysmixQueueBlocks(u64 host_frames, u32 channels, u64 sample_capacity) {
    if (channels == 0) {
        return 0;
    }
    const u64 ring_blocks = sample_capacity / (u64{TargetSampleCount} * channels);
    // Samples of the block being played can still occupy the ring after its metadata is popped.
    const u64 maximum = (std::min<u64>)(ring_blocks > 0 ? ring_blocks - 1 : 0, 64);
    const u64 minimum = (std::min<u64>)(3, maximum);
    const u64 requested = host_frames / TargetSampleCount +
                          (host_frames % TargetSampleCount != 0) + 1;
    return static_cast<u32>(std::clamp(requested, minimum, maximum));
}

} // namespace AudioCore::Sink
