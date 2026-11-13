// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include "common/common_types.h"

namespace VideoCommon {

constexpr u64 TextureGcEffectiveMinAge(u64 base_age, u64 diagnostic_min_age,
                                      bool aggressive_mode) {
    return aggressive_mode ? base_age : (std::max)(base_age, diagnostic_min_age);
}

struct TextureGcWork {
    u64 min_age;
    std::size_t iterations;
};

constexpr TextureGcWork TextureGcWorkFor(u64 used, u64 critical, u64 diagnostic_min_age,
                                         bool high_priority, bool aggressive) {
    TextureGcWork work{
        TextureGcEffectiveMinAge(aggressive ? 10 : high_priority ? 25 : 50,
                                 diagnostic_min_age, aggressive),
        aggressive ? 40U : high_priority ? 20U : 10U,
    };
    if (aggressive && used > critical) {
        const u64 over = used - critical;
        work.iterations += static_cast<std::size_t>((std::min)(over / (u64{2} << 20), u64{1024}));
        if (over >= (u64{1} << 30)) {
            work.min_age = TextureGcEffectiveMinAge(3, diagnostic_min_age, true);
        }
    }
    return work;
}

} // namespace VideoCommon
