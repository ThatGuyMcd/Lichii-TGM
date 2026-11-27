// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>

#include "core/hle/service/nvdrv/nvdata.h"

namespace Service::Nvidia::Devices {

struct NvdecSyncpointIncrement {
    u32 id{};
    u32 increments{};
    u32 unk0{};
    u32 unk1{};
    u32 unk2{};
};
static_assert(sizeof(NvdecSyncpointIncrement) == 0x14);

// Reject an invalid ID before advancing any syncpoints. Thresholds are optional per increment.
template <typename Advance>
[[nodiscard]] std::optional<u32> ApplyNvdecSyncpointIncrements(
    std::span<const NvdecSyncpointIncrement> increments, std::span<u32> thresholds,
    Advance advance) {
    const auto invalid = std::ranges::find_if(increments, [](const auto& increment) {
        return increment.id >= MaxSyncPoints;
    });
    if (invalid != increments.end()) {
        return invalid->id;
    }

    for (std::size_t i = 0; i < increments.size(); ++i) {
        const auto& increment = increments[i];
        const u32 threshold = advance(increment.id, increment.increments);
        if (i < thresholds.size()) {
            thresholds[i] = threshold;
        }
    }
    return std::nullopt;
}

} // namespace Service::Nvidia::Devices
