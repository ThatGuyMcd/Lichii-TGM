// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <utility>

#include "video_core/host1x/nvdec_common.h"

namespace Tegra::Decoders {

[[nodiscard]] inline std::optional<std::pair<u64, u64>> FindH264SurfaceOffsets(
    const Host1x::NvdecCommon::NvdecRegisters& regs, u32 index) {
    if (index >= regs.surface_luma_offsets.size() || index >= regs.surface_chroma_offsets.size()) {
        return std::nullopt;
    }
    return std::pair{regs.surface_luma_offsets[index].Address(),
                     regs.surface_chroma_offsets[index].Address()};
}

} // namespace Tegra::Decoders
