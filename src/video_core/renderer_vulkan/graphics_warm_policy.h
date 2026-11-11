// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "shader_recompiler/frontend/ir/program.h"

namespace Vulkan {

// Standalone vertex programs found by a file scan can require a preceding VertexA program.
// Without that partner, a position read becomes an invalid BuiltIn Position vertex input
// (VUID-Position-Position-04319), which can crash the driver during library compilation.
inline bool CanWarmGraphicsProgram(const Shader::IR::Program& program) {
    return program.stage != Shader::Stage::VertexB ||
           !program.info.loads.AnyComponent(Shader::IR::Attribute::PositionX);
}

} // namespace Vulkan
