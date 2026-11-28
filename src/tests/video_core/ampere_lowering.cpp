// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/ampere/program_capture.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
using Shader::Ampere::ProgramCaptureState;

constexpr Instruction EXIT{{0x0000794d, 0, 0x03800000, 0}};

constexpr Instruction NativeInstruction(u32 opcode, u32 dest, u32 source, u32 word1, u32 word2,
                                        u32 predicate = 7) {
    return {{opcode | (predicate << 12) | (dest << 16) | (source << 24), word1, word2, 0}};
}

// Lowered Maxwell words whose top bits (above `shift`) are `opcode`.
template <typename Program>
size_t CountMaxwell(const Program& lowered, u64 opcode, u32 shift) {
    return static_cast<size_t>(std::count_if(lowered.instructions.begin(),
                                             lowered.instructions.end(),
                                             [&](u64 word) { return (word >> shift) == opcode; }));
}

template <typename Program>
u64 FindMaxwell(const Program& lowered, u64 opcode, u32 shift) {
    const auto it = std::find_if(lowered.instructions.begin(), lowered.instructions.end(),
                                 [&](u64 word) { return (word >> shift) == opcode; });
    return it != lowered.instructions.end() ? *it : 0;
}

TEST_CASE("Ampere BRA.CONV always branches and BRA.DIV never does", "[video_core][ampere]") {
    // BRA.CONV UR6 / BRA.DIV ~URZ to the second EXIT: the mode is in the offset's low two bits.
    const std::array<Instruction, 3> convergent{{
        {{0x06007947, 0x10 | 3, 0x0b800000, 0}},
        EXIT,
        EXIT,
    }};
    ProgramCaptureState capture;
    REQUIRE(capture.Observe(convergent[0], 0) == ProgramCaptureState::Result::Continue);
    const auto taken = LowerToMaxwell(convergent);
    REQUIRE(static_cast<bool>(taken));
    REQUIRE(CountMaxwell(taken, 0xe24, 52) == 1);

    const std::array<Instruction, 3> divergent{{
        {{0xff007947, 0x10 | 2, 0x0b800000, 0}},
        EXIT,
        EXIT,
    }};
    const auto dropped = LowerToMaxwell(divergent);
    REQUIRE(static_cast<bool>(dropped));
    REQUIRE(CountMaxwell(dropped, 0xe24, 52) == 0);
}

TEST_CASE("Ampere HSETP2 loads a constant operand and retains its modifiers",
          "[video_core][ampere]") {
    constexpr u32 bank = 2;
    constexpr u32 offset = 0x54;
    constexpr u32 modifiers = 1U | (1U << 2) | (1U << 3);
    constexpr u32 operand = (offset << 6) | (bank << 22) | (modifiers << 28);
    constexpr u32 compare_and_predicates = (6U << 12) | (2U << 17) | (7U << 20) | (7U << 23);
    const std::array code{NativeInstruction(0x634, 0, 8, operand, compare_and_predicates), EXIT};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    const u64 load = FindMaxwell(lowered, 0x4c98, 48);
    const u64 compare = FindMaxwell(lowered, 0x5d20, 48);
    REQUIRE(load != 0);
    REQUIRE(compare != 0);
    CHECK(((load >> 20) & 0x3fff) == offset / 4);
    CHECK(((load >> 34) & 0x1f) == bank);
    CHECK(((compare >> 20) & 0xff) == (load & 0xff));
    CHECK(((compare >> 28) & 0xf) == modifiers);
}

TEST_CASE("Ampere TLD.SCR.LZ takes its handle from a uniform register", "[video_core][ampere]") {
    constexpr u32 handle = 11, second = 49, mask = 7;
    constexpr u32 descriptor_layout = (1U << 28) | (1U << 29);
    constexpr u32 immediate_level_zero = (7U << 17) | (1U << 23) | (1U << 27);
    const std::array code{
        NativeInstruction(0xf66, 24, 31, 35U | (handle << 8) | descriptor_layout,
                          second | (mask << 8) | immediate_level_zero),
        EXIT,
    };
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    const u64 tld = FindMaxwell(lowered, 0xdd, 56);
    REQUIRE(tld != 0);
    REQUIRE(((tld >> 28) & 7) == 2); // 2D
    REQUIRE(((tld >> 31) & 0xf) == 7);
}

TEST_CASE("Ampere 2D multisample texel fetch keeps the uniform handle and sample in order",
          "[video_core][ampere][ampere_uniform_ms_fetch]") {
    constexpr u32 dest = 39, x = 27, b = 73, handle = 13, second = 91, mask = 0b1101;
    constexpr u32 predicate = 11;
    constexpr u32 ms = 1U << 2;
    const auto fetch = [](u32 flags, u32 dimension, u32 source_a, u32 source_b) {
        const u32 operands = source_b | (handle << 8) | (1U << 28) | (dimension << 29);
        const u32 modes = second | (mask << 8) | (flags << 12) | (7U << 17) | (1U << 23) |
                          (1U << 26) | (1U << 27);
        return NativeInstruction(0xf66, dest, source_a, operands, modes, predicate);
    };

    for (const bool multisample : {false, true}) {
        CAPTURE(multisample);
        const auto lowered = LowerToMaxwell(std::array{fetch(multisample ? ms : 0, 1, x, b), EXIT});
        INFO(lowered.error);
        REQUIRE(static_cast<bool>(lowered));
        REQUIRE(CountMaxwell(lowered, 0xdd, 56) == 1);

        constexpr u32 x_value = 14, y_value = 35, sample = 3, handle_value = 0x894;
        constexpr std::array results{0x27U, 0x3cU, 0x7fU};
        std::array<u32, 256> registers{};
        registers[x] = x_value;
        registers[x + 1] = y_value;
        registers[b] = sample;
        registers[216 + handle] = handle_value;
        registers[dest + 2] = 123;

        // Follow just the emitted register moves and inspect the operands at the fetch.
        for (const u64 word : lowered.instructions) {
            const u32 output = word & 0xff;
            const u32 operand = (word >> 20) & 0xff;
            if ((word >> 48) == 0x5c98) {
                CHECK(((word >> 16) & 0xf) == predicate);
                registers[output] = registers[operand];
            } else if ((word >> 56) == 0xdd) {
                const u32 coordinates = (word >> 8) & 0xff;
                CHECK(((word >> 16) & 0xf) == predicate);
                CHECK(((word >> 28) & 7) == 2); // 2D
                CHECK(((word >> 31) & 0xf) == mask);
                CHECK(((word >> 49) & 1) == 1); // NODEP
                CHECK(((word >> 50) & 1) == multisample);
                CHECK(((word >> 55) & 1) == 0); // Level zero
                CHECK(registers[coordinates] == x_value);
                CHECK(registers[coordinates + 1] == (multisample ? y_value : sample));
                CHECK(registers[operand] == handle_value);
                CHECK(registers[operand + 1] == (multisample ? sample : 0));
                std::copy(results.begin(), results.end(), registers.begin() + output);
            }
        }
        CHECK(registers[dest] == results[0]);
        CHECK(registers[dest + 1] == results[1]);
        CHECK(registers[second] == results[2]);
        CHECK(registers[dest + 2] == 123);
    }

    for (const u32 flag : {1U, 2U, 8U, 16U}) {
        const auto rejected = LowerToMaxwell(std::array{fetch(ms | flag, 1, x, b), EXIT});
        CHECK_FALSE(rejected);
        CHECK(rejected.error == "native bindless texel fetch mode");
    }
    for (const u32 dimension : {0U, 2U, 5U}) {
        const auto rejected = LowerToMaxwell(std::array{fetch(ms, dimension, x, b), EXIT});
        CHECK_FALSE(rejected);
        CHECK(rejected.error == "native bindless texel fetch mode");
    }
    CHECK_FALSE(LowerToMaxwell(std::array{fetch(ms, 1, 215, b), EXIT}));
    CHECK_FALSE(LowerToMaxwell(std::array{fetch(ms, 1, x, 255), EXIT}));
}

TEST_CASE("Ampere register-handle texel fetch spans cannot overwrite uniform shadows",
          "[video_core][ampere]") {
    // Independently assemble TLD.B.LZ from its fields: two or three coordinates followed by
    // a handle. R216 becomes UR0's shadow only when the preceding UMOV is present.
    const auto fetch = [](u32 dest, u32 a, u32 b, u32 second, u32 mask, u32 dimension) {
        return Instruction{{0x7367U | (dest << 16) | (a << 24),
                            b | (1U << 27) | (1U << 28) | (dimension << 29),
                            second | (mask << 8) | (7U << 17) | (1U << 23), 0}};
    };
    constexpr Instruction uniform{{0x00007c02, 0, 0x08000f00, 0}};
    for (const auto instruction : {fetch(4, 215, 12, 255, 3, 1),
                                   fetch(4, 12, 215, 255, 3, 2),
                                   fetch(215, 8, 12, 255, 3, 1),
                                   fetch(4, 8, 12, 215, 15, 1)}) {
        const auto rejected = LowerToMaxwell(std::array{uniform, instruction, EXIT});
        INFO(rejected.error);
        CHECK_FALSE(rejected);
        CHECK(rejected.error == "native bindless texel fetch mode");
        const auto plain = LowerToMaxwell(std::array{instruction, EXIT});
        INFO(plain.error);
        CHECK(static_cast<bool>(plain));
    }
    const auto boundary = LowerToMaxwell(std::array{uniform, fetch(214, 214, 12, 255, 3, 1), EXIT});
    INFO(boundary.error);
    CHECK(static_cast<bool>(boundary));
}

TEST_CASE("Ampere I2F converts signed 16-bit sources", "[video_core][ampere]") {
    constexpr u32 signed_halfword_to_float = (1U << 20) | (1U << 12) | (1U << 10);
    const std::array code{NativeInstruction(0x306, 24, 0, 29, signed_halfword_to_float), EXIT};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    const u64 i2f = FindMaxwell(lowered, 0x5cb, 52);
    REQUIRE(((i2f >> 10) & 3) == 1); // 16-bit source
    REQUIRE(((i2f >> 13) & 1) == 1); // signed
}

TEST_CASE("Ampere integer-to-float conversions take immediates", "[video_core][ampere]") {
    constexpr u32 round_up = (1U << 21) | (9U << 12);
    constexpr u32 truncate = (1U << 21) | (13U << 12);
    const std::array code{
        NativeInstruction(0x906, 22, 0, 52, round_up),
        NativeInstruction(0x845, 31, 0, 325, truncate),
        EXIT,
    };
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(lowered);
    REQUIRE(lowered.instructions.size() == 3);
    CHECK(lowered.instructions[0] ==
          (0x010000000007f000ULL | (u64{std::bit_cast<u32>(52.0f)} << 20) | 22));
    CHECK(lowered.instructions[1] ==
          (0x010000000007f000ULL | (u64{std::bit_cast<u32>(325.0f)} << 20) | 31));
    const Instruction warp_sync{{0x00007348, 0x00000007, 0x03800000, 0x000fe800}};
    const auto rejected = LowerToMaxwell(std::array{code[0], code[1], warp_sync, EXIT});
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error == "unsupported native opcode");
    CHECK(rejected.error_offset == 0x20);
}

TEST_CASE("Ampere convergence token moves leave the gather offset proof intact",
          "[video_core][ampere][convergence_decal]") {
    constexpr auto save = NativeInstruction(0xf55, 2, 19, 0, 0);
    constexpr auto restore = NativeInstruction(0xf56, 2, 19, 0, 1U << 20);
    constexpr auto clear = NativeInstruction(0xf56, 2, 19, 0, 0);
    constexpr auto x = NativeInstruction(0x802, 34, 0, 0x00010100, 15U << 8);
    constexpr auto y = NativeInstruction(0x802, 35, 0, 0xff010000, 15U << 8);
    constexpr auto gather = NativeInstruction(0xb63, 44, 10,
                                              (1U << 29) | (1U << 28) | (2U << 22) | 34,
                                              (7U << 17) | (2U << 12) | (15U << 8) | 255);
    const auto reference = LowerToMaxwell(std::array{x, y, gather, EXIT});
    REQUIRE(reference);
    const auto with_markers = LowerToMaxwell(std::array{x, save, y, restore, gather, clear, EXIT});
    INFO(with_markers.error);
    REQUIRE(with_markers);
    CHECK(with_markers.instructions == reference.instructions);
    CHECK(with_markers.source_instruction_offsets[1] == with_markers.source_instruction_offsets[2]);
    CHECK(with_markers.source_instruction_offsets[3] == with_markers.source_instruction_offsets[4]);

    const auto general_register_form = NativeInstruction(0xf55, 42, 19, 0, 0);
    const auto invalid = LowerToMaxwell(std::array{general_register_form, EXIT});
    CHECK_FALSE(invalid);
    CHECK(invalid.error == "native convergence move mode");

    // Entering the offset sequence through a branch still prevents the constant-offset proof.
    const auto branch_to_y = NativeInstruction(0x947, 0, 0, 0, 0x03800000);
    const auto branch = LowerToMaxwell(std::array{x, save, branch_to_y, y, gather, EXIT});
    CHECK_FALSE(branch);
    CHECK(branch.error == "native per-tap gather control-flow proof");
}

TEST_CASE("Ampere FSWZADD NDV preserves the Maxwell swizzle and registers",
          "[video_core][ampere][convergence_decal]") {
    constexpr u32 rd = 37, ra = 14, rb = 53, swizzle = 0xe4;
    constexpr auto plain = NativeInstruction(0x822, rd, ra, swizzle, rb, 10);
    constexpr auto ndv = NativeInstruction(0x822, rd, ra, swizzle, rb | (1U << 13), 10);
    const auto expected = LowerToMaxwell(std::array{plain, EXIT});
    const auto lowered = LowerToMaxwell(std::array{ndv, EXIT});
    REQUIRE(expected);
    REQUIRE(lowered);
    CHECK(lowered.instructions == expected.instructions);
    const u64 word = lowered.instructions.front();
    CHECK((word >> 48) == 0x50f8);
    CHECK((word & 0xff) == rd);
    CHECK(((word >> 8) & 0xff) == ra);
    CHECK(((word >> 20) & 0xff) == rb);
    CHECK(((word >> 28) & 0xff) == 0xd8); // Maxwell swaps the two bits of each swizzle.
    CHECK(((word >> 16) & 15) == 10);

    const auto invalid = NativeInstruction(0x822, rd, ra, swizzle, rb | (1U << 14), 10);
    CHECK_FALSE(LowerToMaxwell(std::array{invalid, EXIT}));
}

TEST_CASE("Ampere TEX NDV reaches bound and bindless Maxwell samples",
          "[video_core][ampere][convergence_decal]") {
    constexpr u32 xyz = 2U << 29;
    constexpr u32 bound_buffer = 2U << 22;
    constexpr u32 descriptor = 17U << 8;
    constexpr u32 result_mask = 5U << 8;
    constexpr u32 no_second_result = 255;
    constexpr u32 no_sparse_pred = 7U << 17;
    constexpr u32 fields = result_mask | no_second_result | no_sparse_pred;
    constexpr u32 ndv = 1U << 13;
    for (const bool bindless : {false, true}) {
        CAPTURE(bindless);
        const u32 opcode = bindless ? 0x361 : 0xb60;
        const u32 word1 = xyz | (bindless ? 1U << 27 : bound_buffer | descriptor) | 19;
        const auto texture = NativeInstruction(opcode, 60, 11, word1, fields);
        const auto texture_ndv = NativeInstruction(opcode, 60, 11, word1, fields | ndv);
        const auto reference = LowerToMaxwell(std::array{texture, EXIT});
        const auto lowered = LowerToMaxwell(std::array{texture_ndv, EXIT});
        INFO(lowered.error);
        REQUIRE(reference);
        REQUIRE(lowered);
        REQUIRE(lowered.instructions.size() == reference.instructions.size());
        const u64 code = bindless ? 0x37a : 0x30;
        const u32 shift = bindless ? 54 : 58;
        const u64 sample = FindMaxwell(lowered, code, shift);
        REQUIRE(sample != 0);
        CHECK(sample == (FindMaxwell(reference, code, shift) | (u64{1} << 35)));
        CHECK(((sample >> 28) & 7) == 4); // Maxwell 3D texture.
        CHECK(((sample >> 31) & 15) == 5);
        CHECK(lowered.required_texture_buffer == (bindless ? ~0U : 2U));
    }

    const auto first = NativeInstruction(0xb60, 60, 11, xyz | bound_buffer | descriptor | 19,
                                         fields);
    const auto second = NativeInstruction(0xb60, 62, 11, xyz | (4U << 22) | descriptor | 19,
                                          fields | ndv);
    const auto fallback = LowerToMaxwell(std::array{first, second, EXIT});
    REQUIRE(fallback);
    CHECK(fallback.required_texture_buffer == 2);
    const u64 sample = FindMaxwell(fallback, 0x37a, 54);
    REQUIRE(sample != 0);
    CHECK(((sample >> 35) & 1) == 1);
    CHECK(((sample >> 20) & 255) == 246);
}

TEST_CASE("Ampere lane-masked MOV preserves the guest predicate and other lanes",
          "[video_core][ampere][convergence_decal]") {
    constexpr u32 rd = 42, rb = 19;
    constexpr u32 saved_predicate_mask = 1U << 6;
    for (const u32 lane : {0U, 1U, 2U, 3U}) {
        for (const u32 guest : {0U, 6U, 7U, 8U, 14U}) {
            CAPTURE(lane, guest);
            const auto native = NativeInstruction(0x202, rd, 0, rb, 1U << (8 + lane), guest);
            const auto lowered = LowerToMaxwell(std::array{native, EXIT});
            REQUIRE(lowered);
            REQUIRE(lowered.instructions.size() == 8);
            const auto& words = lowered.instructions;
            CHECK((words[0] >> 48) == 0x38e8); // P2R
            CHECK((words[0] & 0xff) == 253);
            CHECK(((words[0] >> 20) & 0x7f) == saved_predicate_mask);
            CHECK((words[1] >> 48) == 0xf0c8); // S2R
            CHECK(((words[1] >> 20) & 0xff) == 0); // SR_LANEID
            CHECK((words[1] & 0xff) == 250);
            CHECK(((words[2] >> 20) & 0xffffffff) == 3);
            CHECK(((words[3] >> 20) & 0xffffffff) == lane);
            CHECK((words[4] >> 52) == 0x5b6); // ISETP.EQ.U32.AND
            CHECK(((words[4] >> 3) & 7) == 6);
            CHECK(((words[4] >> 39) & 15) == guest);
            CHECK(((words[4] >> 49) & 7) == 2);
            CHECK((words[5] >> 48) == 0x5c98); // @P6 MOV
            CHECK(((words[5] >> 16) & 15) == 6);
            CHECK((words[5] & 0xff) == rd);
            CHECK(((words[5] >> 20) & 0xff) == rb);
            CHECK((words[6] >> 48) == 0x38f0); // R2P
            CHECK(((words[6] >> 8) & 0xff) == 253);
            CHECK(((words[6] >> 20) & 0x7f) == saved_predicate_mask);
        }
    }
    const auto two_lanes = NativeInstruction(0x202, rd, 0, rb, 3U << 8);
    const auto rejected = LowerToMaxwell(std::array{two_lanes, EXIT});
    CHECK_FALSE(rejected);
    CHECK(rejected.error == "native MOV modifiers");
}

TEST_CASE("Ampere IMAD return-link tracking distinguishes narrow and pair writes",
          "[video_core][ampere][imad_return_link]") {
    constexpr u32 link = 33;
    constexpr u32 alias = 21;
    constexpr u32 width = sizeof(Instruction);
    constexpr auto set_link = NativeInstruction(0x802, link, 0, 3 * width, 15U << 8);
    constexpr auto narrow = NativeInstruction(0x224, link - 1, 4, 5, 0x078e000b);
    constexpr auto call = NativeInstruction(0x944, 0, 0, width, 0x03c00000);
    constexpr auto copy_link = NativeInstruction(0x224, alias, 255, 255, 0x078e0000 | link);
    constexpr auto ret = NativeInstruction(0x950, 0, alias, 0U - 7 * width, 0x03c3ffff);
    const std::array code{set_link, narrow, call, EXIT, copy_link, narrow, ret};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(lowered);
    CHECK(lowered.instructions.back() == 0xe32000000007000fULL);

    constexpr auto wide = NativeInstruction(0x225, link - 1, 4, 5, 0x078e00ff);
    REQUIRE(LowerToMaxwell(std::array{wide, EXIT}));
    for (const u32 site : {1U, 5U}) {
        CAPTURE(site);
        auto bad = code;
        bad[site] = wide;
        const auto rejected = LowerToMaxwell(bad);
        CHECK_FALSE(rejected);
        CHECK(rejected.error == "native call linkage mode");
    }
    auto known_rz = code;
    known_rz[1] = NativeInstruction(0x224, 255, 4, 5, 0x078e000b);
    CHECK(LowerToMaxwell(known_rz));
}

TEST_CASE("Ampere SUST.P keeps its channel mask", "[video_core][ampere]") {
    // SUST.P.2D.STRONG.SM.R.IGN [R6], R4, 0x0, 0x59
    const std::array<Instruction, 2> code{{
        {{0x06007b9b, 0x60005904, 0x0010a100, 0x000fe200}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(((FindMaxwell(lowered, 0xeb2, 52) >> 20) & 0xf) == 1);
}

TEST_CASE("Ampere SHFL with a register lane keeps its mode", "[video_core][ampere]") {
    // SHFL.DOWN PT, R7, R0, R5, 0x1f
    const std::array<Instruction, 2> code{{
        {{0x00077589, 0x08001f05, 0x000e0000, 0x000fe200}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(((FindMaxwell(lowered, 0xef1, 52) >> 30) & 3) == 2);
}

TEST_CASE("Ampere calls find a return address set ahead of them", "[video_core][ampere]") {
    // MOV R9, 0x30; MOV R4, 0x5; CALL.REL.NOINC; EXIT; RET.REL.NODEC R9
    std::array<Instruction, 5> code{{
        {{0x00097802, 0x30, 0x00000f00, 0}},
        {{0x00047802, 0x5, 0x00000f00, 0}},
        {{0x00007944, 0x10, 0x03c00000, 0}},
        EXIT,
        {{0x09007950, 0xffffffb0, 0x03c3ffff, 0}},
    }};
    REQUIRE(static_cast<bool>(LowerToMaxwell(code)));

    // The link register is written again between its MOV and the call.
    code[1] = {{0x00097802, 0x5, 0x00000f00, 0}};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(!static_cast<bool>(lowered));
    REQUIRE(lowered.error == "native call linkage mode");
}

TEST_CASE("Ampere programs go on past an EXIT that a branch jumps over", "[video_core][ampere]") {
    // A call, then a branch over an EXIT to more of the program, with another call there.
    const std::array<Instruction, 8> code{{
        {{0x00047802, 0x20, 0x00000f00, 0}},
        {{0x00007944, 0x50, 0x03c00000, 0}},
        {{0x00007947, 0x10, 0x03800000, 0}},
        EXIT,
        {{0x00047802, 0x60, 0x00000f00, 0}},
        {{0x00007944, 0x10, 0x03c00000, 0}},
        EXIT,
        {{0x04007950, 0xffffff80, 0x03c3ffff, 0}},
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.source_size == sizeof(code));
}

TEST_CASE("Ampere capture ends at the return of a helper called past the EXIT",
          "[video_core][ampere]") {
    // Independent five-instruction layout: return to EXIT at byte 48 using link R11;
    // CALL at byte 32 targets byte 64. RET adds R11 to next-PC 80 minus 80.
    // MOV byte mask [75:72]=15; CALL/RET relative mode [89:86]=15.
    const std::array<Instruction, 5> code{{
        NativeInstruction(0x802, 7, 0, 0x1357, 15U << 8),
        NativeInstruction(0x802, 11, 0, 48, 15U << 8),
        NativeInstruction(0x944, 0, 0, 16, 15U << 22),
        EXIT,
        NativeInstruction(0x950, 0, 11, static_cast<u32>(-80), (15U << 22) | 0x3ffffU),
    }};
    ProgramCaptureState capture;
    for (u32 index = 0; index + 1 < code.size(); ++index)
        CHECK(capture.Observe(code[index], index) == ProgramCaptureState::Result::Continue);
    CHECK(capture.Observe(code.back(), 4) == ProgramCaptureState::Result::Complete);
    REQUIRE(static_cast<bool>(LowerToMaxwell(code)));

    ProgramCaptureState jump;
    CHECK(jump.Observe(NativeInstruction(0x944, 0, 0, 32, 15U << 22), 0) ==
          ProgramCaptureState::Result::Continue);
    CHECK(jump.Observe(EXIT, 1) == ProgramCaptureState::Result::Continue);
    CHECK(jump.Observe(EXIT, 2) == ProgramCaptureState::Result::Continue);
    CHECK(jump.Observe(EXIT, 3) == ProgramCaptureState::Result::Complete);
}

TEST_CASE("Ampere uniform select and bit operations reach every uniform register",
          "[video_core][ampere]") {
    // Independently assemble @UP2 USEL UR9, UR9, 0x57, UP0. Guard UP2 maps to P4,
    // selector UP0 to P6; both come from R239 and ordinary predicates are restored.
    const std::array<Instruction, 2> select{{
        NativeInstruction(0x887, 9, 9, 0x57, (1U << 27) | (0U << 23), 2),
        EXIT,
    }};
    const auto selected = LowerToMaxwell(select);
    INFO(selected.error);
    REQUIRE(static_cast<bool>(selected));
    const std::array<u64, 5> expected_select{
        0x0100000000070000ULL | (15ULL << 12) | 252 | (0x57ULL << 20),
        0x38e8000000070000ULL | 253 | (255ULL << 8) | (0x50ULL << 20),
        0x38f0000000070000ULL | (239ULL << 8) | (0x50ULL << 20),
        0x5ca0000000000000ULL | (4ULL << 16) | 225 | (225ULL << 8) | (252ULL << 20) | (6ULL << 39),
        0x38f0000000070000ULL | (253ULL << 8) | (0x50ULL << 20)};
    CHECK(std::search(selected.instructions.begin(), selected.instructions.end(),
                      expected_select.begin(), expected_select.end()) != selected.instructions.end());

    // UBREV UR6, UR13, independently chosen uniform registers.
    const std::array<Instruction, 2> reverse{{
        NativeInstruction(0x2be, 6, 0, 13, 1U << 27),
        EXIT,
    }};
    const auto reversed = LowerToMaxwell(reverse);
    INFO(reversed.error);
    REQUIRE(static_cast<bool>(reversed));
    CHECK(reversed.instructions.front() ==
          (0x3828000000070000ULL | 222 | (229ULL << 8) | (1ULL << 39) | (1ULL << 40)));

    // UFLO.U32.SH UR15, UR19: .SH bit 74, PT at 81..83, uniform form at 91.
    const std::array<Instruction, 2> leading{{
        NativeInstruction(0x2bd, 15, 0, 19, (1U << 10) | (7U << 17) | (1U << 27)),
        EXIT,
    }};
    const auto found = LowerToMaxwell(leading);
    INFO(found.error);
    REQUIRE(static_cast<bool>(found));
    CHECK(found.instructions.front() ==
          (0x5c30000000070000ULL | 231 | (235ULL << 20) | (1ULL << 41)));

    // UPOPC UR8, UR12.
    const std::array<Instruction, 2> count{{
        NativeInstruction(0x2bf, 8, 0, 12, 1U << 27),
        EXIT,
    }};
    const auto counted = LowerToMaxwell(count);
    INFO(counted.error);
    REQUIRE(static_cast<bool>(counted));
    CHECK(counted.instructions.front() == (0x5c08000000070000ULL | 224 | (228ULL << 20)));
}

TEST_CASE("Ampere guarded uniform comparisons keep the output when the guard is false",
          "[video_core][ampere]") {
    // Independently assemble @UP1 UISETP.GT.U32.AND UP3, UPT, UR14, 0x45, UPT.
    // Output bits 81..83=3, secondary output 84..86=7, combining predicate 87..89=7;
    // comparison bits 76..78=4 (GT), UPT input bits 68..70=7, uniform form bit 91.
    // P5 guards P3's update. Loading both bits before the comparison retains a false guard's
    // old output; only P3 is merged back before restoring the saved ordinary predicate bits.
    const std::array<Instruction, 2> code{{
        NativeInstruction(0x88c, 0, 14, 0x45,
                          (7U << 4) | (4U << 12) | (3U << 17) | (7U << 20) |
                              (7U << 23) | (1U << 27), 1),
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    INFO(lowered.error);
    REQUIRE(static_cast<bool>(lowered));
    const std::array<u64, 6> expected{
        0x0100000000070000ULL | (15ULL << 12) | 252 | (0x45ULL << 20),
        0x38e8000000070000ULL | 253 | (255ULL << 8) | (0x28ULL << 20),
        0x38f0000000070000ULL | (239ULL << 8) | (0x28ULL << 20),
        0x5b60000000000000ULL | (5ULL << 16) | 7 | (3ULL << 3) | (230ULL << 8) |
            (252ULL << 20) | (7ULL << 39) | (4ULL << 49),
        0x38e8000000070000ULL | 239 | (239ULL << 8) | (8ULL << 20),
        0x38f0000000070000ULL | (253ULL << 8) | (0x28ULL << 20)};
    CHECK(std::equal(expected.begin(), expected.end(), lowered.instructions.begin()));

    // Independent ordinary SEL control: R17, R23, UR5, P2.
    const std::array<Instruction, 2> select{{
        NativeInstruction(0xc07, 17, 23, 5, (2U << 23) | (1U << 27)),
        EXIT,
    }};
    const auto selected = LowerToMaxwell(select);
    INFO(selected.error);
    REQUIRE(static_cast<bool>(selected));
    CHECK(selected.instructions.front() ==
          (0x5ca0000000070000ULL | 17 | (23ULL << 8) | (221ULL << 20) | (2ULL << 39)));
}

TEST_CASE("Ampere unsigned clamps fold to IMNMX on UP0 to UP2", "[video_core][ampere]") {
    // UISETP.LT.U32.AND UPc, UPT, UR4, 0x2, UPT + USEL UR4, UR4, 0x2, UPc:
    // Matching UP0/UP1/UP2 clamps fold USEL to IMNMX.U32 while preserving UISETP R239 write.
    for (const u32 cond : {0U, 1U, 2U}) {
        const auto clamped = LowerToMaxwell(std::array<Instruction, 3>{{
            {{0x0400788c, 0x00000002, 0x0bf01070U | (cond << 17), 0x000fc800}},
            {{0x04047887, 0x00000002, 0x08000000U | (cond << 23), 0x000fc800}},
            EXIT,
        }});
        REQUIRE(static_cast<bool>(clamped));
        CHECK(CountMaxwell(clamped, 0x382003800027dcdcULL, 0) == 1);
        CHECK(CountMaxwell(
            clamped, 0x38e800000007efefULL | (u64{1U << (6U - cond)} << 20), 0) == 1);
    }
    // Mismatched predicate (UP1 vs UP2), negated USEL (!UP1), or guarded UISETP/USEL must not fold.
    for (const auto& non_clamp : {
             std::array<Instruction, 3>{{
                 {{0x0400788c, 0x00000002, 0x0bf21070, 0x000fc800}},
                 {{0x04047887, 0x00000002, 0x09000000, 0x000fc800}}, EXIT}},
             std::array<Instruction, 3>{{
                 {{0x0400788c, 0x00000002, 0x0bf21070, 0x000fc800}},
                 {{0x04047887, 0x00000002, 0x0c800000, 0x000fc800}}, EXIT}},
             std::array<Instruction, 3>{{
                 {{0x0400088c, 0x00000002, 0x0bf21070, 0x000fc800}},
                 {{0x04047887, 0x00000002, 0x08800000, 0x000fc800}}, EXIT}},
             std::array<Instruction, 3>{{
                 {{0x0400788c, 0x00000002, 0x0bf21070, 0x000fc800}},
                 {{0x04040887, 0x00000002, 0x08800000, 0x000fc800}}, EXIT}},
         }) {
        const auto res = LowerToMaxwell(non_clamp);
        REQUIRE(static_cast<bool>(res));
        CHECK(CountMaxwell(res, 0x382003800027dcdcULL, 0) == 0);
    }
}
} // namespace
