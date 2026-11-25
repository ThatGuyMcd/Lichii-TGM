// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "tests/video_core/ampere_instruction_fixture.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;

constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0}};

TEST_CASE("Ampere guarded ULEA without .HI or a carry out lowers", "[video_core][ampere]") {
    using Shader::Ampere::Test::EncodeInstruction;
    constexpr u32 shift = 5, no_carry = 7U << 17, no_carry_in = 15U << 23;
    constexpr u32 uniform_operands = 1U << 27, zero_uniform_register = 63;
    constexpr u32 controls = (shift << 11) | no_carry | no_carry_in | uniform_operands |
                             zero_uniform_register;
    for (const auto lea : {EncodeInstruction(0x891, 12, 18, 0x37, controls, 10),
                           EncodeInstruction(0x891, 9, 12, 0x53, controls, 10)}) {
        const auto lowered = LowerToMaxwell(std::array{lea, Exit});
        INFO(lowered.error);
        REQUIRE(static_cast<bool>(lowered));
        // A carry out (bits 81-83) still needs R253, which holds the swapped-in guard.
        auto carry = lea;
        carry.words[2] &= ~no_carry;
        REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(std::array{carry, Exit})));
    }
}
} // namespace
