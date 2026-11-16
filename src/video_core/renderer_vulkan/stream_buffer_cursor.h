// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>

#include "common/alignment.h"
#include "common/common_types.h"

namespace Vulkan {

/// Reservation policy shared by the Vulkan stream pool and its delayed-completion tests.
class StreamBufferCursor {
public:
    static constexpr size_t NumRegions = 16;
    static constexpr size_t Alignment = 256;

    std::optional<size_t> Reserve(size_t size, size_t capacity, u64 current_tick,
                                  u64 completed_tick) {
        // Heap-budget sizes need not divide evenly into aligned regions. Leave their short tail
        // unused rather than letting an aligned cursor index a seventeenth region.
        capacity -= capacity % (NumRegions * Alignment);
        const size_t region_size = capacity / NumRegions;
        if (region_size == 0 || size == 0 || size > region_size) {
            return std::nullopt;
        }
        const auto region = [region_size](size_t position) { return position / region_size; };
        const auto active = [&](size_t begin, size_t end) {
            return std::any_of(ticks.begin() + begin, ticks.begin() + end,
                               [completed_tick](u64 tick) { return completed_tick < tick; });
        };
        if (active(region(free_iterator) + 1,
                   (std::min)(region(iterator + size) + 1, NumRegions))) {
            return std::nullopt;
        }
        std::fill(ticks.begin() + region(used_iterator), ticks.begin() + region(iterator),
                  current_tick);
        used_iterator = iterator;
        if (iterator + size >= capacity) {
            std::fill(ticks.begin() + region(used_iterator), ticks.end(), current_tick);
            // A refused wrap must not advertise the first region as acquired. Dedicated
            // allocations serve requests until that region's previous submission completes.
            if (active(0, region(size) + 1)) {
                return std::nullopt;
            }
            used_iterator = 0;
            iterator = 0;
            free_iterator = size;
        } else {
            free_iterator = (std::max)(free_iterator, iterator + size);
        }
        const size_t offset = iterator;
        iterator = Common::AlignUp(iterator + size, Alignment);
        return offset;
    }

private:
    size_t iterator{};
    size_t used_iterator{};
    size_t free_iterator{};
    std::array<u64, NumRegions> ticks{};
};

} // namespace Vulkan
