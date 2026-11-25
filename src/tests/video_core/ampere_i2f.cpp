// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;

constexpr Instruction EXIT{{0x0000794d, 0, 0x03800000, 0}};

constexpr Instruction ConvertHalfword(u32 destination, u32 source, bool sign) {
    constexpr u32 float32_halfword = (1U << 20) | (1U << 12);
    return {{0x306U | (7U << 12) | (destination << 16), source,
             float32_halfword | (u32{sign} << 10), 0}};
}

bool Contains(const Shader::Ampere::LoweredProgram& lowered, u64 instruction) {
    return std::find(lowered.instructions.begin(), lowered.instructions.end(), instruction) !=
           lowered.instructions.end();
}

TEST_CASE("Ampere I2F.F32.S16 lowers to a signed 16-bit Maxwell I2F", "[video_core][ampere]") {
    const std::array code{ConvertHalfword(22, 22, true), ConvertHalfword(31, 43, true), EXIT};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    // Maxwell I2F.F32.S16: source format 1 (bits 10-11), signed (bit 13).
    REQUIRE(Contains(lowered, 0x5cb8000000072600ULL | 22 | (22ULL << 20)));
    REQUIRE(Contains(lowered, 0x5cb8000000072600ULL | 31 | (43ULL << 20)));
}

TEST_CASE("Ampere I2F keeps unsigned 16-bit sources unsigned", "[video_core][ampere]") {
    const std::array code{ConvertHalfword(36, 24, false), EXIT};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(Contains(lowered, 0x5cb8000000070600ULL | 36 | (24ULL << 20)));
}

} // namespace
