// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include "common/common_types.h"

namespace Service {

/// Separate from the guest RO mirror, which may already contain a value before the next poll.
template <size_t Count>
class Nx2SyncpointPollSnapshot {
public:
    void Observe(size_t id, u32 value, std::span<u64> changed = {}) {
        if (id >= Count) {
            return;
        }
        if (id / 64 < changed.size() && values[id] != value) {
            changed[id / 64] |= u64{1} << (id % 64);
        }
        values[id] = value;
    }

private:
    std::array<u32, Count> values{};
};

} // namespace Service
