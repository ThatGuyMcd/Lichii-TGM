// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/program_capture.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::ProgramCaptureState;
constexpr Instruction Exit{{0x94dU | (7U << 12), 0, 0x03800000, 0}};
constexpr Instruction Nop{{0x918U | (7U << 12), 0, 0, 0}};
}

TEST_CASE("Ampere branch convergence flags are not address bits", "[ampere][fast_fusion_branch]") {
    for (u32 modifier = 0; modifier < 4; ++modifier) {
        std::array<Instruction, 6> code;
        code.fill(Nop);
        code[0] = Instruction{{0xff007947, 0x40U | modifier, 0x03800000, 0x000fea00}};
        code.back() = Exit;
        ProgramCaptureState capture;
        for (u32 index = 0; index < code.size(); ++index) {
            const auto expected = index + 1 == code.size() ? ProgramCaptureState::Result::Complete
                                                          : ProgramCaptureState::Result::Continue;
            CHECK(capture.Observe(code[index], index) == expected);
        }
        const auto lowered = Shader::Ampere::LowerToMaxwell(code);
        INFO(lowered.error);
        REQUIRE(lowered);
        CHECK(lowered.source_size == sizeof(code));
        if (modifier == 2) {
            REQUIRE(lowered.instructions.size() == 5);
            CHECK(lowered.source_instruction_offsets[0] == lowered.source_instruction_offsets[1]);
            CHECK(lowered.instructions.front() == 0x50b0000000070f00ULL);
        } else {
            CHECK(lowered.instructions.front() == (0xe24000000007000fULL | (0x30ULL << 20)));
        }
    }
}

TEST_CASE("Ampere forward BRA.U preserves its predicate and relative target",
          "[ampere][bananza][fast_fusion_branch]") {
    constexpr u32 source = 11;
    constexpr u32 target = 38;
    constexpr u32 predicate = 8 | 3;
    constexpr u32 offset = (target - source - 1) * sizeof(Instruction);
    std::array<Instruction, target + 1> code;
    code.fill(Nop);
    code[source] = Instruction{{0x947 | (predicate << 12), offset | 1U, 7U << 23, 0}};
    code.back() = Exit;

    const auto lowered = Shader::Ampere::LowerToMaxwell(code);
    INFO(lowered.error);
    REQUIRE(lowered);
    REQUIRE(lowered.instructions.size() == code.size());
    CHECK(lowered.source_size == sizeof(code));
    // Maxwell inserts a control word for each group of three instructions.
    constexpr u64 rel = 8 * (target + target / 3 - source - source / 3 - 1);
    CHECK(lowered.instructions[source] ==
          (0xe24000000000000fULL | (rel << 20) | (u64{predicate} << 16)));
    CHECK(lowered.source_instruction_offsets[target] == target);
}

TEST_CASE("Ampere BRA.U preserves backward targets and both predicates",
          "[ampere][bananza][fast_fusion_branch]") {
    const auto backward = Shader::Ampere::LowerToMaxwell(std::array{
        Nop, Instruction{{0x00008947, 0xffffffe1, 0x0383ffff, 0x000fea00}}, Exit});
    REQUIRE(backward);
    CHECK(backward.instructions[1] == 0xe2400fffff08000fULL);

    const auto guarded = Shader::Ampere::LowerToMaxwell(std::array{
        Instruction{{0x00008947, 0x11, 0x04800000, 0x000fea00}}, Nop, Exit});
    REQUIRE(guarded);
    REQUIRE(guarded.instructions.size() == 4);
    CHECK(guarded.instructions[0] == 0xe24000000100000fULL);
    CHECK(guarded.instructions[1] == 0xe24000000109000fULL);
}

TEST_CASE("Ampere BRA.U rejects unsupported fields and invalid targets",
          "[ampere][bananza][fast_fusion_branch]") {
    const Instruction branch{{0x00008947, 0x1, 0x03800000, 0x000fea00}};
    for (const u32 bit : {82U, 83U, 84U, 85U, 86U, 91U, 92U, 93U, 94U, 95U,
                          96U, 97U, 98U, 99U, 100U, 101U, 102U, 103U, 104U}) {
        auto unsupported = branch;
        unsupported.words[bit / 32] |= 1U << (bit % 32);
        const auto lowered = Shader::Ampere::LowerToMaxwell(std::array{unsupported, Exit});
        INFO(bit);
        REQUIRE_FALSE(lowered);
        CHECK(lowered.error == "native branch convergence mode");
        CHECK(lowered.error_offset == 0);
        CHECK(lowered.instructions.empty());
    }
    for (const u32 offset : {5U, 0x11U, 0xffffffe1U}) {
        auto invalid = branch;
        invalid.words[1] = offset;
        if (offset == 0xffffffe1U) invalid.words[2] |= 0x3ffff;
        const auto lowered = Shader::Ampere::LowerToMaxwell(std::array{invalid, Exit});
        REQUIRE_FALSE(lowered);
        CHECK(lowered.instructions.empty());
    }
}

TEST_CASE("Fast Fusion convergence-mask branch captures its direct target",
          "[ampere][fast_fusion_branch]") {
    const Instruction branch{{0x06007947, 0x00000043, 0x0b800000, 0x000fea00}};
    ProgramCaptureState capture;
    CHECK(capture.Observe(branch, 0x3f0 / 16) == ProgramCaptureState::Result::Continue);
    CHECK(capture.Observe(Exit, 0x400 / 16) == ProgramCaptureState::Result::Continue);
    CHECK(capture.Observe(Exit, 0x440 / 16) == ProgramCaptureState::Result::Complete);

    std::array<Instruction, 6> code;
    code.fill(Nop);
    code[0] = branch;
    code.back() = Exit;
    const auto lowered = Shader::Ampere::LowerToMaxwell(code);
    INFO(lowered.error);
    REQUIRE(lowered);
    CHECK(lowered.instructions.front() == (0xe24000000007000fULL | (0x30ULL << 20)));
}

TEST_CASE("Ampere branch flags preserve negative and invalid destinations",
          "[ampere][fast_fusion_branch]") {
    for (u32 modifier = 0; modifier < 4; ++modifier) {
        ProgramCaptureState backward;
        CHECK(backward.Observe(Instruction{{0xff007947, 0xffffffe0U | modifier, 0x0383ffff,
                                            0x000fea00}}, 1) == ProgramCaptureState::Result::Continue);
        ProgramCaptureState before_start;
        CHECK(before_start.Observe(Instruction{{0xff007947, 0xffffffe0U | modifier, 0x0383ffff,
                                                0x000fea00}}, 0) == ProgramCaptureState::Result::InvalidTarget);
        ProgramCaptureState unaligned;
        CHECK(unaligned.Observe(Instruction{{0xff007947, 0x44U | modifier, 0x03800000,
                                             0x000fea00}}, 0) == ProgramCaptureState::Result::InvalidTarget);
    }
}
