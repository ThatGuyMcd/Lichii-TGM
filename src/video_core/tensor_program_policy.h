// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/settings_enums.h"

namespace VideoCommon {

[[nodiscard]] constexpr bool EmulatesNativeTensorPrograms(Settings::TensorPrograms mode,
                                                         u64 program_id,
                                                         bool cooperative_hmma) noexcept {
    constexpr u64 DonkeyKongBananzaProgramId = 0x040074A01BF12000ULL;
    return program_id == DonkeyKongBananzaProgramId || mode == Settings::TensorPrograms::Native ||
           (mode == Settings::TensorPrograms::Auto && cooperative_hmma);
}

} // namespace VideoCommon
