// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "shader_recompiler/frontend/ir/program.h"

namespace Shader {

inline void LimitLoweredGeometryPassthroughInputs(Info& info) {
    // Geometry shaders can copy Position and generic attributes, not vertex-stage system IDs.
    for (size_t index = 0; index < info.passthrough.mask.size(); ++index) {
        if (index < static_cast<size_t>(IR::Attribute::PositionX) ||
            index > static_cast<size_t>(IR::Attribute::Generic31W)) {
            info.passthrough.mask[index] = false;
        }
    }
}

inline void PruneUnreadGeometryOutputs(IR::Program& geometry, const IR::Program& fragment,
                                      bool transform_feedback, bool native_passthrough) {
    const Info& fs = fragment.info;
    VaryingState& stores = geometry.info.stores;
    if (geometry.stage != Stage::Geometry || fragment.stage != Stage::Fragment ||
        transform_feedback || (geometry.is_geometry_passthrough && native_passthrough) ||
        fs.loads_indexed_attributes || fs.loads.Legacy() || stores.Legacy()) {
        return;
    }
    for (size_t index = 0; index < IR::NUM_GENERICS; ++index) {
        if (!fs.loads.Generic(index)) {
            for (size_t element = 0; element < 4; ++element) {
                stores.Set(IR::Attribute::Generic0X + index * 4 + element, false);
            }
        }
    }
}

} // namespace Shader
