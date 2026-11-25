// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "tests/video_core/ampere_instruction_fixture.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
using Shader::Ampere::Test::ConstantBufferOperand;
using Shader::Ampere::Test::DependencyWait;
using Shader::Ampere::Test::EncodeInstruction;
using Shader::Ampere::Test::SharedCopy;

constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0}};
constexpr u32 ZeroRegister = 255;
constexpr u32 NoCarry = 7U << 17;
constexpr u32 NoCarryIn = 15U << 23;
constexpr u32 HighResult = 1U << 16;

constexpr Instruction ConstantLea(u32 destination, u32 source, u32 bank, u32 offset, u32 shift,
                                  bool high) {
    return EncodeInstruction(0xa11, destination, source, ConstantBufferOperand(bank, offset),
                             ZeroRegister | (shift << 11) | (high ? HighResult : 0) | NoCarry |
                                 NoCarryIn);
}

constexpr auto HighConstantLea = ConstantLea(12, 31, 7, 0x19c, 23, true);

TEST_CASE("Ampere high effective address negates the register base",
          "[video_core][ampere]") {
    constexpr u32 shift = 9, destination = 27, source = 53, base = 18;
    constexpr u32 negate_base = 1U << 31;
    constexpr u32 controls = ZeroRegister | (shift << 11) | HighResult | NoCarry | NoCarryIn;
    for (const u32 guard : {1U, 7U, 9U}) {
        const auto instruction =
            EncodeInstruction(0x211, destination, source, negate_base | base, controls, guard);
        const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        REQUIRE(lowered.instructions.size() == 3);
        CHECK(lowered.instructions[0] ==
              (0x3828000000000000ULL | 250 | (u64{source} << 8) | (u64{guard} << 16) |
               (u64{32 - shift} << 20)));
        CHECK(lowered.instructions[1] ==
              (0x5c11000000000000ULL | destination | (250ULL << 8) | (u64{guard} << 16) |
               (u64{base} << 20)));
    }
    for (const u32 bit : {62U, 74U, 81U}) {
        auto invalid = EncodeInstruction(0x211, destination, source, negate_base | base, controls);
        invalid.words[bit / 32] ^= 1U << (bit % 32);
        CHECK_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
    }
}

TEST_CASE("Ampere surface reductions preserve every U32 atomic operation",
          "[video_core][ampere]") {
    for (u32 op = 0; op < 8; ++op) {
        for (const u32 dimension : {1U, 3U, 5U}) {
            for (const u32 guard : {1U, 7U, 9U}) {
                const Instruction instruction{{0x0400059fU | (guard << 12),
                    0x00008400U | (dimension << 29), 0x0010e000U | (op << 23), 0}};
                const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
                INFO(lowered.error);
                REQUIRE(lowered);
                REQUIRE(lowered.required_texture_buffer == 0);
                CHECK(lowered.instructions[0] == (0xeb48000000000400ULL | (u64{guard} << 16) |
                    (u64{op} << 24) | (u64{dimension} << 33) | (0x84ULL << 36)));
                auto invalid = instruction;
                invalid.words[2] |= 1U << 9;
                CHECK_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
            }
        }
    }
}

TEST_CASE("Ampere LEA.HI with a constant base and no carry lowers", "[video_core][ampere]") {
    const auto lowered = LowerToMaxwell(std::array{HighConstantLea, Exit});
    INFO(lowered.error);
    REQUIRE(static_cast<bool>(lowered));
    const auto& out = lowered.instructions;
    // R250 = R31 >> 9, then R12 = R250 + the constant loaded in R251.
    const u64 load = 0x4c98000000070000ULL | 251 | (15ULL << 39) | (7ULL << 34) |
                     ((0x19cULL / 4) << 20);
    const u64 shift = 0x3828000000070000ULL | 250 | (31ULL << 8) | (9ULL << 20);
    const u64 add = 0x5c10000000070000ULL | 12 | (250ULL << 8) | (251ULL << 20);
    REQUIRE(std::find(out.begin(), out.end(), load) != out.end());
    REQUIRE(std::find(out.begin(), out.end(), shift) != out.end());
    REQUIRE(std::find(out.begin(), out.end(), add) != out.end());
}

TEST_CASE("Ampere LEA.HI with a constant base still rejects a carry out", "[video_core][ampere]") {
    // The same LEA.HI writing a carry out to P0 has no lowering yet.
    auto carry = HighConstantLea;
    carry.words[2] &= ~NoCarry;
    const std::array<Instruction, 2> code{{carry, Exit}};
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(code)));
}

TEST_CASE("Ampere LEA with a constant base and no carry out lowers to ISCADD", "[video_core][ampere]") {
    constexpr auto lea = ConstantLea(36, 24, 2, 0x250, 11, false);
    const auto lowered = LowerToMaxwell(std::array{lea, Exit});
    INFO(lowered.error);
    REQUIRE(static_cast<bool>(lowered));
    // ISCADD R36, R24, R251, 11 with the constant loaded into R251, and no carry compare.
    const auto& out = lowered.instructions;
    const u64 load = 0x4c98000000070000ULL | 251 | (15ULL << 39) | (2ULL << 34) |
                     ((0x250ULL / 4) << 20);
    const u64 add = 0x5c18000000070000ULL | 36 | (24ULL << 8) | (251ULL << 20) | (11ULL << 39);
    REQUIRE(std::find(out.begin(), out.end(), load) != out.end());
    REQUIRE(std::find(out.begin(), out.end(), add) != out.end());
    REQUIRE(std::none_of(out.begin(), out.end(), [](u64 i) { return (i >> 48) == 0x5b60; }));
}

TEST_CASE("Ampere SHF.L.U32.HI immediate without .W lowers to a clamping SHL", "[video_core][ampere]") {
    constexpr u32 high_left_u32 = HighResult | (3U << 9);
    constexpr auto shift = EncodeInstruction(0x419, 44, ZeroRegister, 0xa37c125b, high_left_u32 | 35);
    const auto lowered = LowerToMaxwell(std::array{shift, Exit});
    INFO(lowered.error);
    REQUIRE(static_cast<bool>(lowered));
    // SHL R44, R250, R35 without .W (bit 39), so a shift of 32 or more gives zero.
    const auto& out = lowered.instructions;
    const u64 shl = 0x5c48000000070000ULL | 44 | (250ULL << 8) | (35ULL << 20);
    REQUIRE(std::find(out.begin(), out.end(), shl) != out.end());
}

TEST_CASE("Ampere guarded UIADD3 lowers to a guarded IADD32I or IADD", "[video_core][ampere]") {
    constexpr u32 uniform = 1U << 27;
    constexpr u32 ignored_predicates = (15U << 13) | (63U << 17) | (15U << 23);
    struct Case { u32 opcode, destination, a, b, c, guard, mapped_guard; };
    for (const auto& test : {Case{0x890, 11, 18, 0x73, 63, 10, 12},
                             Case{0x290, 13, 13, 17, 63, 3, 3},
                             Case{0x290, 19, 15, 4, 19, 12, 10}}) {
        const u32 controls = uniform | ignored_predicates | test.c;
        const std::array guarded{EncodeInstruction(test.opcode, test.destination, test.a, test.b,
                                                   controls, test.guard), Exit};
        const std::array plain{EncodeInstruction(test.opcode, test.destination, test.a, test.b,
                                                 controls), Exit};
        const auto lowered = LowerToMaxwell(guarded);
        INFO(lowered.error);
        REQUIRE(static_cast<bool>(lowered));
        const auto reference = LowerToMaxwell(plain);
        REQUIRE(static_cast<bool>(reference));
        // Every add appears predicated on the swapped-in guard.
        const auto& out = lowered.instructions;
        for (const u64 add : reference.instructions) {
            if ((add >> 52) == 0xe30) continue; // EXIT
            const u64 guarded_add = (add & ~(u64{0xf} << 16)) | (u64{test.mapped_guard} << 16);
            REQUIRE(std::find(out.begin(), out.end(), guarded_add) != out.end());
        }
    }
}

TEST_CASE("Ampere DEPBAR.LE preserves shared visibility for SB0 wait lists", "[video_core][ampere]") {
    constexpr auto copy = SharedCopy(45, 32, 20, 0x70, 0x50);
    for (const auto wait : {DependencyWait(3, 4, 0x11), DependencyWait(0, 0, 0x25),
                            DependencyWait(0, 0, 0), DependencyWait(0, 63, 63),
                            DependencyWait(7, 63, 63)}) {
        const std::array code{wait, Exit};
        const auto lowered = LowerToMaxwell(code);
        INFO(lowered.error);
        REQUIRE(static_cast<bool>(lowered));
        REQUIRE(lowered.instructions.size() == 1);
        REQUIRE(lowered.instructions.back() == 0xe30000000007000fULL);
        const auto copying = LowerToMaxwell(std::array{copy, wait, Exit});
        INFO(copying.error);
        REQUIRE(copying);
        const bool shared_wait = wait.Bits(44, 3) == 0;
        REQUIRE(std::count(copying.instructions.begin(), copying.instructions.end(),
                           0xef98000000070000ULL) == (shared_wait ? 1 : 0));
        REQUIRE(copying.source_instruction_offsets[2] - copying.source_instruction_offsets[1] ==
                (shared_wait ? 1U : 0U));
        if (shared_wait) {
            REQUIRE(copying.instructions[copying.source_instruction_offsets[1]] ==
                    0xef98000000070000ULL);
        }
        REQUIRE(copying.instructions.back() == 0xe30000000007000fULL);
    }
}

TEST_CASE("Ampere guarded USHF.R.U32.HI lowers to a guarded SHR", "[video_core][ampere]") {
    constexpr u32 high_right_u32 = (1U << 27) | HighResult | (2U << 11) | (3U << 9);
    constexpr auto guarded_shift_input = EncodeInstruction(0x899, 11, 63, 13, high_right_u32 | 17, 2);
    constexpr auto plain_shift_input = EncodeInstruction(0x899, 11, 63, 13, high_right_u32 | 17);
    const std::array guarded{guarded_shift_input, Exit};
    const std::array plain{plain_shift_input, Exit};
    const auto lowered = LowerToMaxwell(guarded);
    INFO(lowered.error);
    REQUIRE(static_cast<bool>(lowered));
    const auto reference = LowerToMaxwell(plain);
    REQUIRE(static_cast<bool>(reference));
    const u64 shift = reference.instructions.front();
    const u64 guarded_shift = (shift & ~(u64{0xf} << 16)) | (u64{4} << 16);
    const auto& out = lowered.instructions;
    REQUIRE(std::find(out.begin(), out.end(), guarded_shift) != out.end());
}
} // namespace
