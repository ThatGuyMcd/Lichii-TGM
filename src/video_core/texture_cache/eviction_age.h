// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include "common/common_types.h"

namespace VideoCommon {

constexpr u64 TextureGcEffectiveMinAge(u64 base_age, u64 diagnostic_min_age,
                                      bool aggressive_mode) {
    return aggressive_mode ? base_age : (std::max)(base_age, diagnostic_min_age);
}

} // namespace VideoCommon
