// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include "tests/video_core/ampere_instruction_fixture.h"

namespace Shader::Ampere::Test::Trap {
constexpr u32 Rz = 255;
constexpr u32 Pt = 7;
constexpr u32 CbufBank = 0;
constexpr u32 NoCarryOut = 7U << 17;
constexpr u32 NoCarryIn = 15U << 23;
constexpr u32 NoSecondPredicate = 7U << 20;
constexpr u32 RelativeControlFlow = 0x03800000;
constexpr Instruction Exit = EncodeInstruction(0x94d, 0, 0, 0, RelativeControlFlow);
constexpr Instruction Breakpoint = EncodeInstruction(0x95c, 0, 0, 4, 0x00300000);

constexpr Instruction MoveConstant(u32 dest, u32 offset) {
    return EncodeInstruction(0x624, dest, Rz, ConstantBufferOperand(CbufBank, offset),
                             Rz | NoCarryOut | NoCarryIn);
}

enum class Compare : u32 { Equal = 2, LessOrEqual = 3, GreaterOrEqual = 6 };
enum class Combine : u32 { And = 0, Or = 1 };

constexpr Instruction CompareSigned(u32 opcode, u32 reg, u32 operand, Compare comparison, u32 dest,
                                    Combine combine = Combine::And, u32 condition = Pt) {
    constexpr u32 base = 0x70 | (1U << 9);
    const u32 controls = base | NoSecondPredicate | (dest << 17) | (condition << 23) |
                         (static_cast<u32>(comparison) << 12) | (static_cast<u32>(combine) << 10);
    return EncodeInstruction(opcode, 0, reg, operand, controls);
}

constexpr Instruction CompareConstant(u32 reg, u32 offset, Compare comparison, u32 dest,
                                      Combine combine = Combine::And, u32 condition = Pt) {
    return CompareSigned(0xa0c, reg, ConstantBufferOperand(CbufBank, offset), comparison, dest,
                         combine, condition);
}

constexpr Instruction AddConstant(u32 dest, u32 reg, u32 offset) {
    return EncodeInstruction(0xa10, dest, reg, ConstantBufferOperand(CbufBank, offset),
                             Rz | (15U << 13) | (63U << 17) | (15U << 23));
}

constexpr Instruction SkipBreakpoint(u32 guard) {
    return EncodeInstruction(0x947, 0, 0, sizeof(Instruction), RelativeControlFlow, guard);
}

constexpr u32 SingleConstant = 0x3c;
constexpr u32 SingleRegister = 31;
constexpr u32 SingleTrapOffset = 3 * sizeof(Instruction);
constexpr std::array SingleProgram{
    MoveConstant(SingleRegister, SingleConstant),
    CompareSigned(0x20c, SingleRegister, Rz, Compare::GreaterOrEqual, 4),
    SkipBreakpoint(4),
    Breakpoint,
    Exit,
};

// Synthetic byte fill. The sign of either rectangle origin reaches the first
// breakpoint when enabled. A second guard checks the two end coordinates.
constexpr u32 Active = 0x18, X = 0x64, Y = 0x10c;
constexpr u32 Width = 0xa4, Height = 0x38, LimitX = 0x1ac, LimitY = 0xb8;
constexpr u32 XRegister = 37, YRegister = 42, Marker = 56, OrOrigins = 21;
constexpr u32 Right = 68, Bottom = 93;
constexpr u32 Disabled = 2, NonNegative = 3, YFits = 4, RectangleFits = 5;
constexpr u32 RectangleSignTrap = 7 * sizeof(Instruction);
constexpr u32 RectangleBoundsTrap = 15 * sizeof(Instruction);
constexpr std::array RectangleProgram{
    MoveConstant(XRegister, X),
    CompareConstant(Rz, Active, Compare::Equal, Disabled),
    EncodeInstruction(0x807, Marker, Rz, 0x17, Disabled << 23),
    EncodeInstruction(0x388, 0, Rz, Marker | (0x40U << 8), 0),
    EncodeInstruction(0xa12, OrOrigins, XRegister, ConstantBufferOperand(CbufBank, Y),
                      Rz | (0xfcU << 8) | NoCarryOut | NoCarryIn),
    CompareSigned(0x80c, OrOrigins, 0, Compare::GreaterOrEqual, NonNegative, Combine::Or, Disabled),
    SkipBreakpoint(NonNegative),
    Breakpoint,
    MoveConstant(YRegister, Y),
    AddConstant(Bottom, YRegister, Height),
    AddConstant(Right, XRegister, Width),
    CompareConstant(Bottom, LimitY, Compare::LessOrEqual, YFits),
    CompareConstant(Right, LimitX, Compare::LessOrEqual, RectangleFits, Combine::And, YFits),
    CompareConstant(Rz, Active, Compare::Equal, RectangleFits, Combine::Or, RectangleFits),
    SkipBreakpoint(RectangleFits),
    Breakpoint,
    Exit,
};
} // namespace Shader::Ampere::Test::Trap
