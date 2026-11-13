// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include "common/common_types.h"

namespace Core {

// Lichii #445's per-present fence diagnostics, with identity checks for overwritten/pending
// slots. All fields are sequentially consistent so the two identity reads bracket the data.
template <std::size_t Capacity>
class PresentFenceHistory {
    static_assert(Capacity != 0);
public:
    void Record(u64 frame, u64 timestamp, u64 wait_ns) noexcept {
        if (frame == 0) return;
        auto& slot = slots[frame % Capacity];
        slot.frame.store(0);
        slot.timestamp.store(timestamp);
        slot.wait_ns.store(wait_ns);
        slot.frame.store(frame);
    }

    std::optional<u64> Read(u64 frame, u64 timestamp) const noexcept {
        if (frame == 0) return std::nullopt;
        const auto& slot = slots[frame % Capacity];
        if (slot.frame.load() != frame) return std::nullopt;
        const auto actual_timestamp = slot.timestamp.load();
        const auto wait = slot.wait_ns.load();
        if (slot.frame.load() != frame || actual_timestamp != timestamp) return std::nullopt;
        return wait;
    }

private:
    struct Slot {
        std::atomic<u64> frame{}, timestamp{}, wait_ns{};
    };
    std::array<Slot, Capacity> slots{};
};

} // namespace Core
