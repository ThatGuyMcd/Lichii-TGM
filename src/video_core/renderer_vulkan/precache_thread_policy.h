// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>

#include "common/common_types.h"

namespace Vulkan {

constexpr size_t SelectPrecacheThreads(size_t hardware_threads, u32 setting, size_t forced,
                                      u64 system_commit, u64 system_commit_limit) {
    if (forced != 0) {
        return forced;
    }
    if (setting != 0) {
        return std::clamp<size_t>(setting, 1, 64);
    }
    const size_t all = std::clamp<size_t>(hardware_threads, 1, 64);
    if (system_commit_limit == 0) {
        return all;
    }
    if (system_commit >= system_commit_limit) {
        return 1;
    }
    constexpr u64 BytesPerThread = 2ULL << 30;
    constexpr u64 Headroom = 2ULL << 30;
    const u64 free_commit = system_commit_limit - system_commit;
    const size_t fit = static_cast<size_t>(
        std::max<u64>(free_commit > Headroom ? (free_commit - Headroom) / BytesPerThread : 0, 1));
    return std::min(all, fit);
}

} // namespace Vulkan
