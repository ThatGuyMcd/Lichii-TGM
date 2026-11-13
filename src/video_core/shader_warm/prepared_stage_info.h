// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>

#include "shader_recompiler/shader_info.h"

namespace VideoCommon::ShaderWarm {

/// These infos depend on other stages or pipeline state, so require a whole-key match.
[[nodiscard]] inline bool CanReusePreparedStageInfo(const Shader::Info& info,
                                                   bool object_motion_active = false) {
    return !object_motion_active && !info.loads.Legacy() && !info.requires_layer_emulation &&
           !info.binds_unmapped_pointer_word &&
           !info.oot_motion.history && !info.oot_motion.image && !info.oot_motion.shadow &&
           std::ranges::none_of(info.texture_descriptors,
                                [](const auto& descriptor) { return descriptor.is_descriptor_array; });
}

} // namespace VideoCommon::ShaderWarm
