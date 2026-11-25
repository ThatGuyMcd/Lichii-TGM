// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/program_capture.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::ProgramCaptureState;
using Result = ProgramCaptureState::Result;

constexpr u32 Always = 7;
constexpr u32 ZeroRegister = 255;
constexpr u32 FirstBodyExit = 7;
enum class Carry { Add, MultiplyAdd };

constexpr u32 Opcode(u32 opcode, u32 dest = 0, u32 source = 0, u32 guard = Always) {
    return opcode | (guard << 12) | (dest << 16) | (source << 24);
}

constexpr Instruction MoveImmediate(u32 dest, u32 value) {
    return {{Opcode(0x802, dest), value, 15U << 8, 0}};
}

constexpr Instruction Exit(u32 guard = Always) {
    return {{Opcode(0x94d, 0, 0, guard), 0, Always << 23, 0}};
}

constexpr Instruction Nop{{Opcode(0x918), 0, 0, 0}};

// The fixed registers and carry fields satisfy the exit setup decoder. The body,
// scheduling words and handler location are chosen independently for this test.
constexpr std::array<Instruction, 5> InstallHandler(u32 address, Carry carry) {
    const Instruction add_high{{Opcode(0x210, 1, 1), ZeroRegister,
        ZeroRegister | (1U << 10) | (15U << 13) | (Always << 17) | (Always << 20), 0}};
    const Instruction imad_high{{Opcode(0x224, 1, ZeroRegister), ZeroRegister,
        1U | (1U << 10) | (Always << 17), 0}};
    return {{
        {{Opcode(0x34e), 0, 0, 0}}, // LEPC R0:R1
        MoveImmediate(3, address),
        // IADD3 R0,P0,R0,R3,RZ: carry out P0, unused second carry and inputs.
        {{Opcode(0x210), 3,
          ZeroRegister | (15U << 13) | (Always << 20) | (15U << 23), 0}},
        carry == Carry::Add ? add_high : imad_high,
        {{Opcode(0x357), 0, 0, 0}}, // install the computed exit target
    }};
}

template <size_t Handler>
constexpr auto MakeProgram(Carry carry) {
    static_assert(Handler >= 12);
    std::array<Instruction, Handler + 3> code{};
    code.fill(Nop);
    const auto setup = InstallHandler(Handler * sizeof(Instruction), carry);
    for (size_t index = 0; index < setup.size(); ++index) {
        code[index] = setup[index];
    }
    // R0 must be redefined before any body exit finalizes the output handle. The
    // other body operands and all offsets are choices made for this test.
    code[5] = MoveImmediate(0, 43);
    code[6] = MoveImmediate(19, 0x2468);
    code[FirstBodyExit] = Exit();
    code[8] = MoveImmediate(21, 0x1357);
    code[Handler - 2] = Exit(11);
    code[Handler - 1] = Exit();
    code[Handler] = {{Opcode(0x324, ZeroRegister), ZeroRegister, 0, 0}};
    code[Handler + 1] = {{Opcode(0x957), 0, 0, 0}};
    code[Handler + 2] = Exit();
    return code;
}

template <size_t N>
void RequireCompleteAt(const std::array<Instruction, N>& code, u32 last) {
    ProgramCaptureState capture;
    for (u32 index = 0; index <= last; ++index) {
        INFO("instruction " << index << " expected last " << last);
        REQUIRE(capture.Observe(code[index], index) ==
                (index == last ? Result::Complete : Result::Continue));
    }
}

template <size_t Handler>
void RequireRecognized(Carry carry) {
    const auto code = MakeProgram<Handler>(carry);
    RequireCompleteAt(code, code.size() - 1);
    const auto envelope = Shader::Ampere::AnalyzeGeometryExit(code);
    INFO(envelope.error);
    REQUIRE(envelope);
    CHECK(envelope.handler == Handler * sizeof(Instruction));
    REQUIRE(envelope.exits.size() == 3);
    CHECK(envelope.exits[0].offset == FirstBodyExit * sizeof(Instruction));
    CHECK(envelope.exits[1].predicate == 11);

    const auto lowered = Shader::Ampere::LowerToMaxwell(code, true);
    INFO(lowered.error);
    REQUIRE(lowered);
    CHECK(lowered.source_size == sizeof(code));
    REQUIRE(lowered.source_instruction_offsets.size() == code.size());
    const auto address = [](u32 index) { return 0x50U + index * 8 + ((index + 1) / 3) * 8; };
    const u32 handler = lowered.source_instruction_offsets[Handler];
    for (const auto& exit : envelope.exits) {
        const u32 index = lowered.source_instruction_offsets[exit.offset / sizeof(Instruction)];
        const u64 branch = lowered.instructions[index];
        const u64 encoded_offset = (branch >> 20) & 0xffffff;
        const s64 relative = static_cast<s64>(encoded_offset ^ 0x800000) - 0x800000;
        CHECK((branch & ~(u64{0xffffff} << 20)) ==
              (0xe24000000000000fULL | (u64{exit.predicate} << 16)));
        CHECK(s64{address(index)} + 8 + relative == address(handler));
    }
}

TEST_CASE("Ampere exit capture and lowering retain independently placed handlers",
          "[video_core][ampere][geometry_capture]") {
    for (const Carry carry : {Carry::Add, Carry::MultiplyAdd}) {
        RequireRecognized<12>(carry);
        RequireRecognized<23>(carry);
        RequireRecognized<41>(carry);
    }
}

TEST_CASE("Ampere exit capture requires exact setup operands",
          "[video_core][ampere][geometry_capture]") {
    for (const Carry carry : {Carry::Add, Carry::MultiplyAdd}) {
        for (u32 setup = 0; setup < 5; ++setup) {
            for (u32 word = 0; word < 3; ++word) {
                // The target is a parameter, not a fixed instruction operand.
                if (setup == 1 && word == 1) {
                    continue;
                }
                for (u32 bit = 0; bit < 32; ++bit) {
                    auto code = MakeProgram<23>(carry);
                    code[setup].words[word] ^= 1U << bit;
                    CAPTURE(setup, word, bit);
                    RequireCompleteAt(code, FirstBodyExit);
                }
            }
        }
    }
}

TEST_CASE("Ampere exit capture bounds the handler destination",
          "[video_core][ampere][geometry_capture]") {
    for (const Carry carry : {Carry::Add, Carry::MultiplyAdd}) {
        for (const u32 target : {0U, 4U * 16, 23U * 16 + 1,
                                 ProgramCaptureState::MaximumInstructions * 16}) {
            auto code = MakeProgram<23>(carry);
            code[1].words[1] = target;
            ProgramCaptureState capture;
            for (u32 index = 0; index < 4; ++index) {
                REQUIRE(capture.Observe(code[index], index) == Result::Continue);
            }
            REQUIRE(capture.Observe(code[4], 4) == Result::InvalidTarget);
        }
        // Capture accepts the minimum aligned destination; the stricter geometry
        // analyzer still rejects this overlapping handler location.
        auto minimum = MakeProgram<23>(carry);
        minimum[1].words[1] = 5 * 16;
        RequireCompleteAt(minimum, FirstBodyExit);
        CHECK_FALSE(Shader::Ampere::AnalyzeGeometryExit(minimum));

        auto maximum = MakeProgram<23>(carry);
        maximum[1].words[1] = (ProgramCaptureState::MaximumInstructions - 1) * 16;
        ProgramCaptureState capture;
        for (u32 index = 0; index < maximum.size(); ++index) {
            REQUIRE(capture.Observe(maximum[index], index) == Result::Continue);
        }
    }
}

TEST_CASE("Ampere exit capture rejects extended modes but ignores scheduling",
          "[video_core][ampere][geometry_capture]") {
    for (const Carry carry : {Carry::Add, Carry::MultiplyAdd}) {
        for (u32 setup = 0; setup < 5; ++setup) {
            for (u32 bit = 0; bit < 32; ++bit) {
                auto code = MakeProgram<23>(carry);
                code[setup].words[3] = 1U << bit;
                CAPTURE(setup, bit);
                RequireCompleteAt(code, bit >= 9 && bit < 30 ? code.size() - 1 : FirstBodyExit);
            }
        }
    }
}

TEST_CASE("Ampere unrecognized exit setup does not terminate at a predicated exit",
          "[video_core][ampere][geometry_capture]") {
    for (const Carry carry : {Carry::Add, Carry::MultiplyAdd}) {
        for (const u32 guard : {3U, 11U, 15U}) {
            auto code = MakeProgram<23>(carry);
            code[0] = Nop;
            code[6] = Exit(guard);
            RequireCompleteAt(code, FirstBodyExit);
        }
    }
}

TEST_CASE("Ampere capture does not authorize invalid geometry lowering",
          "[video_core][ampere][geometry_capture]") {
    for (const Carry carry : {Carry::Add, Carry::MultiplyAdd}) {
        auto code = MakeProgram<23>(carry);
        const auto wrong_stage = Shader::Ampere::LowerToMaxwell(code);
        REQUIRE_FALSE(wrong_stage);
        CHECK(wrong_stage.instructions.empty());
        CHECK(wrong_stage.error_offset == 0);
        CHECK(wrong_stage.error == "native program counter load outside geometry stage");

        // The recognizer sees the complete envelope, but the body would consume
        // the setup's live R0 as its output handle. Capture must not waive validation.
        code[5] = Nop;
        RequireCompleteAt(code, code.size() - 1);
        const auto bad_body = Shader::Ampere::LowerToMaxwell(code, true);
        REQUIRE_FALSE(bad_body);
        CHECK(bad_body.instructions.empty());
        CHECK(bad_body.error_offset == 5 * sizeof(Instruction));
        CHECK(bad_body.error == "native instruction before exit setup state is dead");
    }
}
} // namespace
