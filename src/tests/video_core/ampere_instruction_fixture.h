// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "shader_recompiler/frontend/ampere/program_capture.h"

namespace Shader::Ampere::Test {
constexpr Instruction EncodeInstruction(u32 opcode, u32 destination, u32 source, u32 operand,
                                        u32 controls, u32 guard = 7) {
    return {{opcode | (guard << 12) | (destination << 16) | (source << 24), operand, controls, 0}};
}

constexpr u32 ConstantBufferOperand(u32 bank, u32 byte_offset) {
    return (bank << 22) | (byte_offset << 6);
}

constexpr Instruction DependencyWait(u32 scoreboard, u32 count, u32 mask) {
    constexpr u32 less_or_equal = 1U << 15;
    return EncodeInstruction(0x91a, 0, 0,
                             less_or_equal | (scoreboard << 12) | (count << 6) | mask, 0);
}

constexpr Instruction SharedCopy(u32 shared, u32 global, u32 descriptor, u32 shared_offset,
                                 u32 global_offset) {
    constexpr u32 descriptor_copy_128 = 0x0c101c00;
    constexpr u32 always_enabled = 7U << 23;
    return EncodeInstruction(0xfae, shared, global, (shared_offset / 16 << 16) | global_offset,
                             descriptor_copy_128 | always_enabled | descriptor);
}
} // namespace Shader::Ampere::Test
