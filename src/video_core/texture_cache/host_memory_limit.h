// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <limits>

#include "common/common_types.h"

namespace VideoCommon {

/// Space the four forced collections of a host-spill episode from actual collections.
class HostSpillCollections {
public:
    static constexpr u32 Limit = 4;

    bool TryCollect(u64 frame) {
        if (count == Limit || (count != 0 && frame - last_frame < 15)) {
            return false;
        }
        last_frame = frame;
        ++count;
        return true;
    }

    u32 Count() const noexcept {
        return count;
    }

    void Reset() noexcept {
        count = 0;
        last_frame = 0;
    }

private:
    u64 last_frame{};
    u32 count{};
};

/// Native titles refresh their device budget every 30 frames. Ramp their cache ceiling across
/// 8--10 GiB rather than making a large step when a changing budget crosses 10 GiB.
constexpr u64 TextureGcDeviceCeiling(u64 budget, u64 base_ceiling, bool native) {
    constexpr u64 GiB = u64{1} << 30;
    u64 ceiling = (std::max)(base_ceiling, budget / 8 * 5);
    if (budget >= 10 * GiB) {
        ceiling = (std::max)(ceiling, budget - (5 * GiB / 2));
    } else if (native && budget > 8 * GiB) {
        ceiling = (std::max)(ceiling, 5 * GiB + (budget - 8 * GiB) * 5 / 4);
    }
    return ceiling;
}

/// Commit the texture cache leaves to the rest of the process and the system.
constexpr u64 HostCommitReserve = u64{2} << 30;

/// The cache is not shrunk below this for host memory: a scene needs about this much, and under
/// it the collector only deletes and decodes the same textures again.
constexpr u64 MinHostCriticalMemory = u64{3} << 30;

/// The most device memory the texture cache may hold before it collects in earnest, as host
/// memory allows it. Windows charges device memory to the system's commit limit (RAM plus the
/// page files) like any other memory, since each allocation has a backing store there. A 16 GB
/// card's budget alone let the cache fill 9.7 GiB on a PC with 16 GiB of RAM and a 16 GiB page
/// file, where the guest's memory already takes 14 GiB of commit: the driver then refused a
/// 2.6 MiB image in every heap, host memory included, and the session ended (Ocarina of Time,
/// RX 7600 XT). `device_usage` is the device memory in use now and `commit_room` the commit the
/// system can still hand out: their sum is what the cache would hold with all of it.
constexpr u64 HostCriticalMemory(u64 device_usage, u64 commit_room) {
    const u64 limit = commit_room > (std::numeric_limits<u64>::max)() - device_usage
                          ? (std::numeric_limits<u64>::max)()
                          : device_usage + commit_room;
    return (std::max)(limit - (std::min)(limit, HostCommitReserve), MinHostCriticalMemory);
}

/// Apply after device/heap adjustments so they cannot raise the cache past the host's room.
constexpr void ClampToHostCriticalMemory(u64 limit, u64 margin, u64& expected, u64& critical,
                                         u64& minimum) {
    if (limit >= critical) {
        return;
    }
    critical = limit;
    expected = (std::min)(expected, critical - (std::min)(margin, critical / 2));
    minimum = (std::min)(minimum, expected - (std::min)(expected, u64{1} << 30));
}

} // namespace VideoCommon
