// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include "common/common_types.h"

namespace Vulkan {

/// Bounded descriptor storage within one frame. Submission belongs to the caller's draw boundary.
class DescriptorFrameCursor {
public:
    struct Allocation { size_t chunk; u64 offset; };

    bool Fits(u64 size, u64 capacity, size_t chunks) const noexcept {
        return size <= capacity && chunks != 0 &&
               (size <= capacity - cursor || chunk + 1 < chunks);
    }

    std::optional<Allocation> Allocate(u64 size, u64 capacity, size_t chunks) noexcept {
        if (!Fits(size, capacity, chunks)) {
            return std::nullopt;
        }
        if (size > capacity - cursor) {
            ++chunk;
            cursor = 0;
        }
        const Allocation result{chunk, cursor};
        cursor += size;
        return result;
    }

    void Reset() noexcept { chunk = 0; cursor = 0; }

private:
    size_t chunk{};
    u64 cursor{};
};

} // namespace Vulkan
