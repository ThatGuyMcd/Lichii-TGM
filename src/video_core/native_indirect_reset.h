// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <limits>
#include <optional>
#include <span>
#include "common/common_types.h"

namespace VideoCommon::NativeIndirectReset {

inline constexpr size_t ProgramByteCount = 11 * 16;
inline constexpr std::array<u32, 5> Record{0, 1, 0, 0, 0};

using Fingerprint = std::array<u8, 32>;
bool MatchesFingerprint(std::span<const u8> code, const Fingerprint& expected);
bool Matches(std::span<const u8> code);

struct ListRange {
    u64 address;
    size_t bytes;
};

inline std::optional<ListRange> GetListRange(u64 list, u32 list_size, u32 first, u32 count) {
    const u64 entries = list_size / sizeof(u64);
    // Refuse oversized draws, rather than silently resetting only a prefix.
    if (!list || count == 0 || count > (1U << 20) || first > entries || count > entries - first) {
        return std::nullopt;
    }
    const u64 offset = u64{first} * sizeof(u64);
    const u64 bytes = u64{count} * sizeof(u64);
    if (list > (std::numeric_limits<u64>::max)() - offset - bytes) {
        return std::nullopt;
    }
    return ListRange{list + offset, static_cast<size_t>(bytes)};
}

} // namespace VideoCommon::NativeIndirectReset
