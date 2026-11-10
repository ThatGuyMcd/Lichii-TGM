// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "video_core/renderer_vulkan/fixed_pipeline_state.h"

namespace Vulkan {

/// A disabled static logic operation cannot affect the pipeline. A dynamic enable carries no
/// recorded value, so its operation must remain intact (Fritchii commit 9ea66c8273).
inline void CanonicalizeNativeLogicOp(FixedPipelineState& state,
                                     const DynamicFeatures& features) noexcept {
    if (!features.has_dynamic_state3_logic_op_enable && !state.dynamic_state.logic_op_enable) {
        state.dynamic_state.logic_op.Assign(0);
    }
}

/// NVIDIA records a dynamic operation that AMD may need static. Only the known-disabled case
/// can be converted; all other device feature checks and live environment validation still apply.
inline void NormalizeRecordedNativeLogicOp(FixedPipelineState& state,
                                          const DynamicFeatures& features) noexcept {
    if (state.extended_dynamic_state_2_logic_op &&
        !features.has_extended_dynamic_state_2_logic_op &&
        !features.has_dynamic_state3_logic_op_enable && !state.dynamic_state.logic_op_enable) {
        state.extended_dynamic_state_2_logic_op.Assign(0);
    }
    CanonicalizeNativeLogicOp(state, features);
}

} // namespace Vulkan
