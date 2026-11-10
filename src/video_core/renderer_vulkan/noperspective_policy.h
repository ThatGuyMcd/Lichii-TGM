// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "shader_recompiler/runtime_info.h"

namespace Vulkan {

inline void ApplyNoPerspectiveInterpolationPolicy(Shader::RuntimeInfo& info, u32 vendor_id) {
    // The AMD clipping workaround produces white OoT fog on NVIDIA. Keep its native
    // interpolation, removing the paired producer and consumer transforms together.
    if (vendor_id == 0x10DE) {
        info.noperspective_outputs.reset();
        info.scaled_noperspective_inputs = false;
    }
}

} // namespace Vulkan
