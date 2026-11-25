// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Trap guard regression coverage adapted from Duck912/eden-fefw at 8c6f052.
#include <array>
#include <unordered_map>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/ampere/trap_guards.h"
#include "tests/video_core/ampere_trap_fixture.h"

namespace Fixture = Shader::Ampere::Test::Trap;

TEST_CASE("Uniform native trap guards reject changed constants and unknown inputs",
          "[video_core][ampere][fire_emblem][trap_guards]") {
    using Shader::Ampere::Test::EncodeInstruction;
    constexpr auto input = Fixture::SingleProgram;
    u32 constant{};
    const Shader::Ampere::TrapGuardReader read = [&](u32 bank, u32 offset) -> std::optional<u32> {
        REQUIRE(bank == Fixture::CbufBank);
        REQUIRE(offset == Fixture::SingleConstant);
        return constant;
    };
    const auto prefix = std::span{input}.first(4);
    for (u32 value : {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
        constant = value;
        REQUIRE(Shader::Ampere::VerifyTrapGuards(prefix, read) == (value < 0x80000000U));
    }
    REQUIRE_FALSE(Shader::Ampere::VerifyTrapGuards(prefix, {}));
    REQUIRE_FALSE(
        Shader::Ampere::VerifyTrapGuards(prefix, [](u32, u32) { return std::optional<u32>{}; }));
    constant = 0;
    auto unknown = input;
    unknown[0] = EncodeInstruction(0x224, Fixture::SingleRegister, Fixture::Rz, Fixture::Rz,
                                   Fixture::NoCarryOut | Fixture::NoCarryIn | 19);
    REQUIRE_FALSE(Shader::Ampere::VerifyTrapGuards(std::span{unknown}.first(4), read));
    unknown = input;
    unknown[0] = EncodeInstruction(0x919, Fixture::SingleRegister, 0, 0, 0x1500);
    REQUIRE_FALSE(Shader::Ampere::VerifyTrapGuards(std::span{unknown}.first(4), read));
    unknown = input;
    unknown[2].words[1] = 0xfffffff0;
    REQUIRE_FALSE(Shader::Ampere::VerifyTrapGuards(std::span{unknown}.first(4), read));
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
    const std::array traps{Fixture::SingleTrapOffset};
    REQUIRE(Shader::Ampere::LowerToMaxwell(input, false, false, traps));
    const auto load = EncodeInstruction(0x984, Fixture::SingleRegister, Fixture::Rz, 0, 0);
    const std::array overwritten{input[0], load, input[1], input[2], input[3]};
    REQUIRE_FALSE(Shader::Ampere::VerifyTrapGuards(overwritten, read));
}

TEST_CASE("Synthetic byte fill validates both rectangle bounds on every dispatch",
          "[video_core][ampere][fire_emblem][trap_guards]") {
    using namespace Fixture;
    constexpr auto code = RectangleProgram;
    std::unordered_map<u32, u32> constants{
        {X, 3}, {Y, 7}, {Width, 597}, {Height, 327}, {LimitX, 600}, {LimitY, 334}, {Active, 1},
    };
    const auto read = [&](u32 bank, u32 offset) -> std::optional<u32> {
        if (bank != CbufBank || !constants.contains(offset))
            return std::nullopt;
        return constants.at(offset);
    };
    const auto prefix = std::span{code}.first(code.size() - 1);
    REQUIRE(Shader::Ampere::VerifyTrapGuards(prefix, read));
    for (const u32 offset : {X, Y}) {
        const u32 start = constants.at(offset);
        constants[offset] = start + 1; // Rectangle exceeds its buffer by one pixel.
        CHECK_FALSE(Shader::Ampere::VerifyTrapGuards(prefix, read));
        constants[offset] = 0xffffffffU; // Negative origin reaches the first trap.
        CHECK_FALSE(Shader::Ampere::VerifyTrapGuards(prefix, read));
        constants[offset] = start;
        CHECK(Shader::Ampere::VerifyTrapGuards(prefix, read));
    }
    constants.erase(LimitY);
    CHECK_FALSE(Shader::Ampere::VerifyTrapGuards(prefix, read));
    constants[LimitY] = 334;
    CHECK(Shader::Ampere::VerifyTrapGuards(prefix, read));
    constexpr std::array traps{RectangleSignTrap, RectangleBoundsTrap};
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(code));
    const auto lowered = Shader::Ampere::LowerToMaxwell(code, false, false, traps);
    INFO(lowered.error);
    REQUIRE(lowered);
    constexpr std::array partial{RectangleSignTrap};
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(code, false, false, partial));
    auto invalid = code;
    invalid[RectangleBoundsTrap / sizeof(Shader::Ampere::Instruction)].words[1] = 8;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid, false, false, traps));
}
