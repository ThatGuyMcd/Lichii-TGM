// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <utility>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/geometry_exit.h"
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::AnalyzeGeometryExit;
using Shader::Ampere::Instruction;

constexpr u32 Always = 7;
constexpr u32 Zero = 255;
constexpr u32 BranchPredicate = Always << 23;
constexpr u32 Handler = 19;
constexpr u32 GuardedExit = Handler - 2;
constexpr u32 LastBodyExit = Handler - 1;
constexpr u32 BodyGuard = 2;

constexpr u32 Address(u32 index) {
    return index * static_cast<u32>(sizeof(Instruction));
}

constexpr Instruction Encode(u32 opcode, u32 dest = 0, u32 a = 0, u32 b = 0, u32 modifiers = 0,
                             u32 guard = Always) {
    return {{opcode | (guard << 12) | (dest << 16) | (a << 24), b, modifiers, 0}};
}

constexpr Instruction Move(u32 dest, u32 immediate) {
    return Encode(0x802, dest, 0, immediate, 15U << 8);
}

constexpr Instruction Exit(u32 guard = Always) {
    return Encode(0x94d, 0, 0, 0, BranchPredicate, guard);
}

constexpr Instruction Relative(u32 opcode, u32 from, u32 to, u32 reg = 0, u32 flags = 0) {
    const u64 displacement = static_cast<u64>(s64{Address(to)} - Address(from + 1));
    return Encode(opcode, 0, reg, static_cast<u32>(displacement),
                  (static_cast<u32>(displacement >> 32) & 0x3ffffU) | BranchPredicate | flags);
}

constexpr Instruction Permute(u32 a, u32 guard = Always) {
    constexpr u32 IdentityLowPair = 0x5410;
    return Encode(0x816, 1, a, IdentityLowPair, Zero, guard);
}

constexpr Instruction Compare(u32 b, u32 combine_predicate = Always) {
    constexpr u32 Equal = 2;
    return Encode(0x20c, 0, 1, b, (Equal << 12) | (Always << 20) | (combine_predicate << 23));
}

constexpr Instruction ImadCarry = Encode(0x224, 1, Zero, Zero, 1U | (1U << 10) | (Always << 17));
constexpr Instruction Iadd3Carry = Encode(0x210, 1, 1, Zero,
                                         Zero | (1U << 10) | (15U << 13) |
                                             (Always << 17) | (Always << 20));
constexpr Instruction Nop = Encode(0x918);

// Build the fixed exit protocol from its operand fields, with a new body and handler address.
constexpr auto Program = [] {
    std::array<Instruction, Handler + 3> code{};
    code.fill(Nop);
    code[0] = Encode(0x34e);
    code[1] = Move(3, Address(Handler));
    code[2] = Encode(0x210, 0, 0, 3, Zero | (15U << 13) | (Always << 20) | (15U << 23));
    code[3] = ImadCarry;
    code[4] = Encode(0x357);
    constexpr u32 InvocationId = 17;
    code[5] = Encode(0x919, 0, 0, 0, InvocationId << 8);
    code[6] = Relative(0x945, 6, 10);
    code[7] = Permute(0);
    code[8] = Compare(12);
    code[9] = Move(3, 61);
    code[10] = Encode(0x941, 0, 0, 0, BranchPredicate);
    code[12] = Move(19, 0x5a31);
    code[GuardedExit] = Exit(BodyGuard);
    code[LastBodyExit] = Exit();
    code[Handler] = Encode(0x324, Zero, 0, Zero);
    code[Handler + 1] = Encode(0x957);
    code[Handler + 2] = Exit();
    return code;
}();

template <size_t N>
void RequireAccepted(const std::array<Instruction, N>& code) {
    const auto envelope = AnalyzeGeometryExit(code);
    INFO(envelope.error << " at " << envelope.error_offset);
    REQUIRE(static_cast<bool>(envelope));
    REQUIRE(envelope.handler == Address(Handler));
}

template <size_t N>
void RequireRejected(const std::array<Instruction, N>& code, std::string_view error, u32 offset) {
    const auto envelope = AnalyzeGeometryExit(code);
    REQUIRE(envelope.error == error);
    REQUIRE(envelope.error_offset == offset);
    REQUIRE(envelope.exits.empty());
    REQUIRE(envelope.handler == 0);
}

TEST_CASE("Ampere geometry exit envelope routes body exits to the handler", "[video_core][ampere]") {
    for (const Instruction& carry : {ImadCarry, Iadd3Carry}) {
        auto code = Program;
        code[3] = carry;
        const auto envelope = AnalyzeGeometryExit(code);
        REQUIRE(static_cast<bool>(envelope));
        REQUIRE(envelope.handler == Address(Handler));
        REQUIRE(envelope.exits.size() == 2);
        REQUIRE(envelope.exits[0].offset == Address(GuardedExit));
        REQUIRE(envelope.exits[0].predicate == BodyGuard);
        REQUIRE(envelope.exits[1].offset == Address(LastBodyExit));
        REQUIRE(envelope.exits[1].predicate == 7);
    }
}

TEST_CASE("Ampere geometry exit envelope requires the exact prologue and handler",
          "[video_core][ampere]") {
    for (const u32 index : {0U, 1U, 2U, 3U, 4U, Handler, Handler + 1, Handler + 2}) {
        for (u32 word = 0; word < 4; ++word) {
            for (u32 bit = 0; bit < 32; ++bit) {
                auto code = Program;
                code[index].words[word] ^= 1U << bit;
                const bool scheduling = word == 3 && bit >= 9 && bit <= 29;
                INFO("instruction " << index << " word " << word << " bit " << bit);
                REQUIRE(static_cast<bool>(AnalyzeGeometryExit(code)) == scheduling);
            }
        }
    }
}

TEST_CASE("Ampere geometry exit envelope bounds the whole program", "[video_core][ampere]") {
    const std::span<const Instruction> program{Program};
    for (const u32 size : {0U, 5U, 8U, Handler, Handler + 2})
        REQUIRE(!AnalyzeGeometryExit(program.first(size)));
    std::array<Instruction, Program.size() + 1> longer{};
    std::copy(Program.begin(), Program.end(), longer.begin());
    longer.back() = Exit();
    RequireRejected(longer, "native exit handler location", 0x10);
    for (const u32 target : {Address(5), Address(6), Address(Handler - 1),
                             Address(Handler) + 8, Address(Handler + 1)}) {
        auto code = Program;
        code[1].words[1] = target;
        RequireRejected(code, "native exit handler location", 0x10);
    }
}

TEST_CASE("Ampere geometry exit handler is not reached by fallthrough", "[video_core][ampere]") {
    auto code = Program;
    code[LastBodyExit] = Exit(BodyGuard);
    RequireRejected(code, "native exit handler reached by fallthrough", Address(LastBodyExit));
    code[LastBodyExit] = Nop;
    RequireRejected(code, "native exit handler reached by fallthrough", Address(LastBodyExit));
}

TEST_CASE("Ampere geometry exit setup results must be dead before the body reads them",
          "[video_core][ampere]") {
    constexpr std::string_view reads = "native body reads exit setup state";
    constexpr std::string_view unmodeled = "native instruction before exit setup state is dead";
    auto code = Program;
    code[7] = Permute(1);
    RequireRejected(code, reads, 0x70);

    code = Program;
    code[5].words[0] &= ~(Always << 12);
    code[5].words[0] |= 1U << 12;
    RequireRejected(code, reads, 0x70);

    code = Program;
    code[7] = Permute(0, 0);
    RequireRejected(code, reads, 0x70);

    code = Program;
    code[8] = Compare(3);
    RequireRejected(code, reads, 0x80);

    code = Program;
    code[8] = Compare(12, 0);
    RequireRejected(code, reads, 0x80);

    code = Program;
    code[7] = Encode(0x224, 1, 0, Zero, Zero | (1U << 10) | (Always << 17));
    RequireRejected(code, reads, 0x70);
    code[7].words[2] &= ~(1U << 10);
    RequireAccepted(code);

    code = Program;
    code[9] = Nop;
    RequireRejected(code, unmodeled, 0x90);

    code = Program;
    code[6] = Exit(1);
    RequireRejected(code, unmodeled, 0x60);

    code = Program;
    code[10] = Nop;
    RequireAccepted(code);

    // Writing R3 through ALD kills its setup value only when the vertex index is independent.
    code = Program;
    constexpr u32 AttributeOffset = 0x94;
    code[9] = Encode(0x321, 3, Zero, (AttributeOffset << 8) | 9U);
    RequireAccepted(code);
    code[9].words[1] = (AttributeOffset << 8) | 3U;
    RequireRejected(code, reads, 0x90);
    // OUT.EMIT updates R3 if its input handle comes from another register.
    code = Program;
    code[9] = Encode(0x324, 3, 9, Zero, 1U << 14);
    RequireAccepted(code);
    code[9] = Encode(0x324, 3, 3, Zero, 1U << 14);
    RequireRejected(code, reads, 0x90);
}

TEST_CASE("Ampere geometry exit tracks the reverse-encoded global load predicate",
          "[video_core][ampere]") {
    constexpr Instruction load = Encode(0x381, 12, 18, 0,
                                        (1U << 8) | (4U << 9) | (1U << 15) |
                                            (Always << 17) | (1U << 20));
    for (const u32 predicate : {0U, 6U, 8U, 14U}) {
        auto code = Program;
        code[6] = load;
        code[6].words[2] |= predicate;
        RequireAccepted(code);
    }
    for (const u32 predicate : {7U, 15U}) {
        auto code = Program;
        code[6] = load;
        code[6].words[2] |= predicate;
        RequireRejected(code, "native body reads exit setup state", 0x60);

        // The original ISETP at index 8 overwrites P0 before this load reads it.
        code = Program;
        code[9] = load;
        code[9].words[0] = Encode(0x381, 3, 18).words[0];
        code[9].words[2] |= predicate;
        RequireAccepted(code);
    }
}

TEST_CASE("Ampere geometry exit body cannot touch the handler state", "[video_core][ampere]") {
    for (const u32 opcode : {0x357U, 0x957U, 0x355U, 0x34eU}) {
        auto code = Program;
        code[10] = Encode(opcode);
        RequireRejected(code, "native exit handler state access in body", 0xa0);
    }
}

TEST_CASE("Ampere geometry exit body control flow stays inside the body", "[video_core][ampere]") {
    constexpr std::string_view leaves = "native branch leaves exit envelope body";
    constexpr std::string_view control = "native control flow in exit envelope body";
    auto code = Program;
    code[10] = Relative(0x947, 10, 13);
    RequireAccepted(code);
    code[10] = Relative(0x947, 10, Handler);
    RequireRejected(code, leaves, 0xa0);
    code[10] = Relative(0x947, 10, 4);
    RequireRejected(code, leaves, 0xa0);

    code = Program;
    code[6] = Relative(0x945, 6, Handler);
    RequireRejected(code, leaves, 0x60);

    code = Program;
    code[10] = Relative(0x944, 10, 11);
    RequireRejected(code, control, 0xa0);
    code[10].words[2] |= 1U << 22;
    RequireAccepted(code);

    for (const u32 opcode : {0x950U, 0x949U, 0x95bU, 0x94aU}) {
        code = Program;
        code[10] = Relative(opcode, 10, 11);
        RequireRejected(code, control, 0xa0);
    }
}

TEST_CASE("Ampere geometry exit body EXITs have no extra modes", "[video_core][ampere]") {
    constexpr std::string_view mode = "native body EXIT mode";
    auto code = Program;
    code[GuardedExit].words[2] = (Always - 1) << 23;
    RequireRejected(code, mode, Address(GuardedExit));
    code = Program;
    code[GuardedExit].words[2] |= 1U << 20;
    RequireRejected(code, mode, Address(GuardedExit));
    code = Program;
    code[GuardedExit].words[3] |= 1;
    RequireRejected(code, mode, Address(GuardedExit));
    code = Program;
    code[GuardedExit] = Exit(Always | 8);
    RequireRejected(code, mode, Address(GuardedExit));
}
TEST_CASE("Ampere geometry exit lowering branches body exits to the handler",
          "[video_core][ampere]") {
    const auto lowered = Shader::Ampere::LowerToMaxwell(Program, true);
    INFO(lowered.error << " at " << lowered.error_offset);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.source_size == sizeof(Program));
    const auto& offsets = lowered.source_instruction_offsets;
    const auto& code = lowered.instructions;
    REQUIRE(offsets.size() == Program.size());
    for (size_t index = 0; index < 5; ++index) REQUIRE(offsets[index + 1] == offsets[index]);
    REQUIRE(offsets[Handler + 2] == offsets[Handler + 1]);
    REQUIRE(offsets[Handler + 2] + 1 == code.size());

    const auto address_of = [](u32 index) { return 0x50U + index * 8 + ((index + 1) / 3) * 8; };
    const auto branch_target = [&](u32 index) {
        const s64 relative = static_cast<s64>((code[index] >> 20) & 0xffffff) << 40 >> 40;
        return s64{address_of(index)} + 8 + relative;
    };
    const u32 handler = offsets[Handler];
    for (const auto& [native, guard] : {std::pair{GuardedExit, BodyGuard}, {LastBodyExit, Always}}) {
        const u32 branch = offsets[native];
        REQUIRE(offsets[native + 1] == branch + 1);
        REQUIRE((code[branch] & ~(u64{0xffffff} << 20)) == (0xe24000000000000fULL | (u64{guard} << 16)));
        REQUIRE(branch_target(branch) == address_of(handler));
    }
    REQUIRE(offsets[LastBodyExit] == offsets[GuardedExit] + 1);

    REQUIRE(offsets[Handler + 1] == handler + 1);
    const u64 final_output = code[handler];
    REQUIRE((final_output & 0xfff8000000000000ULL) == 0xfbe0000000000000ULL);
    REQUIRE((final_output & 0xff) == 0xff);
    REQUIRE(((final_output >> 8) & 0xff) == 0);
    REQUIRE(((final_output >> 16) & 0xf) == 7);
    REQUIRE(((final_output >> 20) & 0xff) == 0xff);
    REQUIRE(((final_output >> 39) & 3) == 0);

    REQUIRE(code.back() == 0xe30000000007000fULL);
    REQUIRE(std::count_if(code.begin(), code.end(), [](u64 word) {
                return (word & 0xfff0000000000000ULL) == 0xe300000000000000ULL;
            }) == 1);
}

TEST_CASE("Ampere geometry calls return before the exit handler", "[video_core][ampere][kirby]") {
    constexpr u32 Link = 26;
    constexpr u32 Call = 14;
    constexpr u32 Return = 17;
    constexpr u32 CallHandler = Return + 1;
    constexpr u32 NoStackAdjustment = 1U << 22;
    std::array<Instruction, CallHandler + 3> program{};
    program.fill(Nop);
    std::copy_n(Program.begin(), 10, program.begin());
    program[1] = Move(3, Address(CallHandler));
    program[Call - 1] = Move(Link, Address(Call + 1));
    program[Call] = Relative(0x944, Call, Return, 0, NoStackAdjustment);
    program[Call + 1] = Exit();
    program[Return] = Relative(0x950, Return, 0, Link, NoStackAdjustment);
    std::copy_n(Program.begin() + Handler, 3, program.begin() + CallHandler);

    const auto lowered = Shader::Ampere::LowerToMaxwell(program, true);
    INFO(lowered.error << " at " << lowered.error_offset);
    REQUIRE(lowered);
    REQUIRE(lowered.source_size == sizeof(program));
    REQUIRE(lowered.source_instruction_offsets.size() == program.size());
    const auto& offsets = lowered.source_instruction_offsets;
    CHECK(lowered.instructions[offsets[Return]] == 0xe32000000007000fULL);
    CHECK((lowered.instructions[offsets[CallHandler]] & 0xfff8000000000000ULL) ==
          0xfbe0000000000000ULL);
    CHECK(lowered.instructions.back() == 0xe30000000007000fULL);
    const auto address = [](u32 i) { return 0x50U + i * 8 + ((i + 1) / 3) * 8; };
    for (const auto& [source, target] : {std::pair{Call, Return}, {Call + 1, CallHandler}}) {
        const u64 instruction = lowered.instructions[offsets[source]];
        const s64 relative = static_cast<s64>((instruction >> 20) & 0xffffff) << 40 >> 40;
        CHECK(s64{address(offsets[source])} + 8 + relative == address(offsets[target]));
    }

    auto invalid = program;
    invalid[Return].words[0] &= ~(Always << 12);
    CHECK_FALSE(Shader::Ampere::LowerToMaxwell(invalid, true));
    invalid = program;
    invalid[Return].words[1] += sizeof(Instruction);
    CHECK_FALSE(Shader::Ampere::LowerToMaxwell(invalid, true));
    invalid = program;
    invalid[Return] = Relative(0x950, Return, 0, Link + 1, NoStackAdjustment);
    const auto unlinked = Shader::Ampere::LowerToMaxwell(invalid, true);
    CHECK_FALSE(unlinked);
    CHECK(unlinked.error == "native return linkage mode");
    invalid = program;
    invalid[Call - 1].words[1] += sizeof(Instruction);
    CHECK_FALSE(Shader::Ampere::LowerToMaxwell(invalid, true));
}
} // namespace
