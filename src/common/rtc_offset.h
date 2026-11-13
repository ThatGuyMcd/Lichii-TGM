// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <limits>
#include "common/common_types.h"

namespace Common {
constexpr s64 ApplyRtcOffset(s64 now, s64 offset) {
    if (offset > 0 && now > (std::numeric_limits<s64>::max)() - offset) {
        return (std::numeric_limits<s64>::max)();
    }
    if (offset < 0 && now < (std::numeric_limits<s64>::min)() - offset) {
        return 0;
    }
    const s64 value = now + offset;
    return value < 0 ? 0 : value;
}
} // namespace Common
