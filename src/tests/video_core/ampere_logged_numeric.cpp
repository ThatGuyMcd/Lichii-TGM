// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/maxwell/structured_control_flow.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"
#include "tests/video_core/ampere_instruction_fixture.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
namespace IR = Shader::IR;

constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0}};
// Independent instruction assembly: opcode [11:0], guard [15:12], destination [23:16],
// first source [31:24]. The remaining fields are supplied explicitly by each synthetic case;
// scheduling/reuse bits are zero. No captured instruction words are used by these cases.
constexpr Instruction SyntheticInstruction(u32 opcode, u32 dest, u32 source, u32 operand,
                                           u32 controls, u32 guard = 7) {
    return {{opcode | (guard << 12) | (dest << 16) | (source << 24), operand, controls, 0}};
}

constexpr u32 F2IDestination = 19;
constexpr u32 F2IBank = 5;
constexpr u32 F2IOffset = 0x1ac;
constexpr u32 AbsoluteCbuf = 1U << 30;
constexpr u32 NegatedCbuf = 1U << 31;
constexpr u32 CbufOperand = Shader::Ampere::Test::ConstantBufferOperand(F2IBank, F2IOffset);
constexpr Instruction AbsoluteF2I =
    SyntheticInstruction(0xb05, F2IDestination, 0, AbsoluteCbuf | CbufOperand, 0x00212800);

struct ShiftFixture {
    u32 destination;
    u32 shift_register;
    u32 value;

    constexpr Instruction Encode() const {
        constexpr u32 unsigned_high_left = 0x00010600;
        return SyntheticInstruction(0x419, destination, 255, value,
                                    unsigned_high_left | shift_register);
    }
};
constexpr std::array ShiftFixtures{ShiftFixture{29, 41, 0x34ab911d},
                                   ShiftFixture{63, 57, 0xe2095b37}};

u32 Address(u32 index) {
    return 0x50 + index * 8 + ((index + 1) / 3) * 8;
}

class LoggedEnvironment final : public Shader::Environment {
public:
    explicit LoggedEnvironment(std::span<const u64> code_) : code{code_} {
        stage = Shader::Stage::Compute;
    }

    u64 ReadInstruction(u32 address) override {
        for (u32 index = 0; index < code.size(); ++index) {
            if (Address(index) == address)
                return code[index];
        }
        FAIL("Read outside logged instruction fixture at " << address);
        return 0;
    }
    u32 ReadCbufValue(u32, u32) override {
        FAIL("Unexpected constant buffer specialization");
        return 0;
    }
    Shader::TextureType ReadTextureType(u32) override {
        return Shader::TextureType::Color2D;
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return Shader::TexturePixelFormat::A8B8G8R8_UNORM;
    }
    bool IsTexturePixelFormatInteger(u32) override { return false; }
    u32 ReadViewportTransformState() override { return 1; }
    u32 TextureBoundBuffer() const override { return 0; }
    u32 LocalMemorySize() const override { return 0; }
    u32 SharedMemorySize() const override { return 4096; }
    std::array<u32, 3> WorkgroupSize() const override { return {32, 1, 1}; }
    bool HasHLEMacroState() const override { return false; }
    bool HasNativeInstructions() const override { return true; }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}

private:
    std::span<const u64> code;
};

u32 EvaluateShift(IR::Value value) {
    value = value.Resolve();
    if (value.IsImmediate()) {
        if (value.Type() == IR::Type::F32) {
            return std::bit_cast<u32>(value.F32());
        }
        return value.Type() == IR::Type::U1 ? u32{value.U1()} : value.U32();
    }
    const auto& inst = *value.Inst();
    const auto arg = [&](size_t index) { return EvaluateShift(inst.Arg(index)); };
    switch (inst.GetOpcode()) {
    case IR::Opcode::Identity:
        return arg(0);
    case IR::Opcode::BitCastU32F32:
    case IR::Opcode::BitCastF32U32:
        return arg(0);
    case IR::Opcode::FPFma32:
        return std::bit_cast<u32>(std::fma(std::bit_cast<float>(arg(0)),
                                         std::bit_cast<float>(arg(1)),
                                         std::bit_cast<float>(arg(2))));
    case IR::Opcode::FPMax32:
        return std::bit_cast<u32>(
            std::max(std::bit_cast<float>(arg(0)), std::bit_cast<float>(arg(1))));
    case IR::Opcode::BitwiseAnd32:
        return arg(0) & arg(1);
    case IR::Opcode::BitwiseOr32:
        return arg(0) | arg(1);
    case IR::Opcode::ULessThan:
        return arg(0) < arg(1);
    case IR::Opcode::SelectU32:
        return arg(0) ? arg(1) : arg(2);
    case IR::Opcode::ShiftLeftLogical32: {
        const u32 shift = arg(1);
        if (shift >= 32)
            throw std::domain_error("Unmasked out-of-range shift reaches the result");
        return arg(0) << shift;
    }
    default:
        throw std::logic_error(std::string{IR::NameOf(inst.GetOpcode())});
    }
}

void TranslateAndEmit(std::span<const u64> code, std::optional<u32> expected = std::nullopt) {
    LoggedEnvironment env{code};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, Shader::Maxwell::Location{0x50}};
    Shader::ObjectPool<IR::Inst> inst_pool;
    Shader::ObjectPool<IR::Block> block_pool;
    auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg,
                                                    Shader::HostTranslateInfo{});
    u32 stores = 0;
    for (const auto* block : program.blocks) {
        for (const auto& inst : block->Instructions()) {
            if (inst.GetOpcode() != IR::Opcode::WriteSharedU32)
                continue;
            ++stores;
            if (expected) {
                INFO(IR::DumpProgram(program));
                CHECK(EvaluateShift(inst.Arg(1)) == *expected);
            }
        }
    }
    REQUIRE(stores == 1);
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010400;
    const auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
    REQUIRE(spirv.size() > 5);
    CHECK(spirv.front() == 0x07230203);
}

u64 Store(u32 source) {
    return 0xef58000000070000ULL | source | (255ULL << 8) | (4ULL << 48);
}
} // namespace

TEST_CASE("Synthetic absolute constant F2I retains U16 rounding and reaches SPIRV",
          "[video_core][ampere][logged_numeric]") {
    const auto lowered = LowerToMaxwell(std::array{AbsoluteF2I, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    const std::array<u64, 6> expected{
        0x4c98000000070000ULL | 252 | (15ULL << 39) | (u64{F2IOffset / 4} << 20) |
            (u64{F2IBank} << 34),
        0x0400000000070000ULL | 252 | (252ULL << 8) | (0x7fffffffULL << 20),
        0x5800000000070000ULL | 250 | (252ULL << 8) | (252ULL << 20) |
            (7ULL << 39) | (7ULL << 48),
        0x5c40000000070000ULL | 250 | (252ULL << 8) | (250ULL << 20) | (7ULL << 48),
        0x5cb0000000070000ULL | F2IDestination | (1ULL << 8) | (2ULL << 10) |
            (250ULL << 20) | (1ULL << 44),
        0xe30000000007000fULL,
    };
    REQUIRE(lowered.instructions.size() == expected.size());
    CHECK(std::equal(expected.begin(), expected.end(), lowered.instructions.begin()));
    auto code = lowered.instructions;
    code.insert(code.end() - 1, Store(F2IDestination));
    TranslateAndEmit(code);
}

TEST_CASE("Synthetic F2I preserves guards and rejects unrelated modifiers",
          "[video_core][ampere][logged_numeric]") {
    for (const u32 guard : {0U, 6U, 7U, 8U, 14U}) {
        auto instruction = AbsoluteF2I;
        instruction.words[0] = (instruction.words[0] & ~0xf000U) | (guard << 12);
        const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
        REQUIRE(lowered);
        for (size_t index = 0; index + 1 < lowered.instructions.size(); ++index)
            CHECK(((lowered.instructions[index] >> 16) & 15) == guard);
    }
    for (const u32 changes : {NegatedCbuf ^ AbsoluteCbuf, NegatedCbuf, 1U << 26, 1U << 6,
                              1U << 27, 1U << 28, 1U << 29}) {
        auto invalid = AbsoluteF2I;
        invalid.words[1] ^= changes;
        CHECK_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
    }
    for (const u32 bit : {0U, 8U, 30U, 31U}) {
        auto invalid = AbsoluteF2I;
        invalid.words[3] ^= 1U << bit;
        CHECK_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
    }
    auto invalid = AbsoluteF2I;
    invalid.words[0] |= 1U << 24;
    CHECK_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
    invalid = AbsoluteF2I;
    invalid.words[2] |= 1U << 27;
    CHECK_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
    invalid = SyntheticInstruction(0xb05, 28, 0,
                                   AbsoluteCbuf | Shader::Ampere::Test::ConstantBufferOperand(9, 0x158),
                                   0x0021e100);
    CHECK_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
}

TEST_CASE("Synthetic SHF clamps instead of wrapping through production translation",
          "[video_core][ampere][logged_numeric]") {
    for (const auto& fixture : ShiftFixtures) {
        for (const bool wrap : {false, true}) {
            auto variant = fixture.Encode();
            variant.words[2] |= u32{wrap} << 11;
            const auto lowered = LowerToMaxwell(std::array{variant, Exit});
            INFO(lowered.error);
            REQUIRE(lowered);
            REQUIRE(lowered.instructions.size() == 3);
            CHECK(lowered.instructions[0] ==
                  (0x0100000000070000ULL | 250 | (15ULL << 12) | (u64{fixture.value} << 20)));
            CHECK(lowered.instructions[1] ==
                  (0x5c48000000070000ULL | fixture.destination | (250ULL << 8) |
                   (u64{fixture.shift_register} << 20) | (u64{wrap} << 39)));
            for (const u32 shift : {0U, 1U, 7U, 24U, 31U, 32U, 33U, 63U, 0xffffffffU}) {
                auto code = lowered.instructions;
                code.insert(code.begin(), 0x0100000000070000ULL | fixture.shift_register |
                                              (15ULL << 12) | (u64{shift} << 20));
                code.insert(code.end() - 1, Store(fixture.destination));
                const u32 effective = wrap ? shift & 31 : std::min(shift, 32U);
                const u32 expected = effective == 32 ? 0U : fixture.value << effective;
                CAPTURE(shift, wrap);
                TranslateAndEmit(code, expected);
            }
        }
    }
}

TEST_CASE("Synthetic SHF register pressure identifies the register shift operand",
          "[video_core][ampere][logged_numeric]") {
    for (const u32 high : {216U, 217U, 240U, 246U, 254U}) {
        const auto instruction = ShiftFixture{high, high, ShiftFixtures[0].value}.Encode();
        const auto pressure = Shader::Ampere::PrepareRegisterPressure(instruction, 216);
        REQUIRE(pressure.valid);
        CHECK(pressure.prefix.size() == 8);
        CHECK(pressure.suffix.size() == 8);
        CHECK(pressure.instruction.Bits(16, 8) < 216);
        CHECK(pressure.instruction.Bits(64, 8) == pressure.instruction.Bits(16, 8));
        CHECK(pressure.instruction.Bits(24, 8) == 255);
        CHECK(pressure.instruction.words[1] == ShiftFixtures[0].value);
        REQUIRE(LowerToMaxwell(std::array{pressure.instruction, Exit}));
    }
    for (const u32 bit : {8U, 12U, 17U, 27U}) {
        auto invalid = ShiftFixtures[0].Encode();
        invalid.words[2] ^= 1U << bit;
        CHECK_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
    }
    auto invalid = ShiftFixtures[0].Encode();
    invalid.words[0] &= ~0xff000000U;
    CHECK_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
}

TEST_CASE("TXD register pressure covers the layer and a single destination",
          "[video_core][ampere][logged_numeric]") {
    // dimension at bit 61, second destination at bit 64, channel mask at bit 72.
    const auto txd = [](u32 dest, u32 coordinates, u32 dimension, u32 mask) {
        return Instruction{{0xb6c | (dest << 16) | (coordinates << 24), (dimension << 29), 255U | (mask << 8), 0}};
    };
    // ARRAY_2D reads three coordinates: the layer follows the pair in the same group.
    auto pressure = Shader::Ampere::PrepareRegisterPressure(txd(0, 216, 5, 0x3), 216);
    REQUIRE(pressure.valid);
    CHECK(pressure.instruction.Bits(24, 8) < 216);
    // ... and one in the next group cannot be copied with the pair: rejected, not misread.
    CHECK_FALSE(Shader::Ampere::PrepareRegisterPressure(txd(0, 218, 5, 0x3), 216).valid);
    CHECK(Shader::Ampere::PrepareRegisterPressure(txd(0, 218, 1, 0x3), 216).valid);
    // A single channel writes one register, which may be the last of a group.
    pressure = Shader::Ampere::PrepareRegisterPressure(txd(247, 0, 1, 0x1), 216);
    REQUIRE(pressure.valid);
    CHECK(pressure.instruction.Bits(16, 8) < 216);
    CHECK_FALSE(Shader::Ampere::PrepareRegisterPressure(txd(247, 0, 1, 0x3), 216).valid);
}

namespace {
constexpr u32 FlushDenormals = 1U << 16;
constexpr u32 UnitMultiplyScale = 4U << 20;
constexpr auto UniformBankMarker = SyntheticInstruction(0xc02, 0, 0, 0, 0, 15);

constexpr Instruction SyntheticLoad(u32 destination, u32 address, u32 byte_offset) {
    // LDG.E.32 with an even register address pair and a signed byte offset starting at bit 40.
    constexpr u32 controls = (15U << 17) | (1U << 15) | (4U << 9) | (1U << 8);
    return SyntheticInstruction(0x381, destination, address, byte_offset << 8, controls);
}

// Independent operands shared by the remapping checks and scalar execution checks below.
constexpr auto RegisterAdd = SyntheticInstruction(0x221, 231, 137, 73, FlushDenormals);
constexpr auto ImmediateMultiply = SyntheticInstruction(0x820, 234, 72, std::bit_cast<u32>(23.5f),
                                                         FlushDenormals | UnitMultiplyScale);
constexpr auto ImmediateAdd = SyntheticInstruction(0x421, 68, 224, std::bit_cast<u32>(1.25f),
                                                    FlushDenormals);
constexpr auto RegisterFma = SyntheticInstruction(0x223, 225, 233, 132, FlushDenormals | 51U);
constexpr auto ImmediateFma = SyntheticInstruction(0x823, 123, 241, std::bit_cast<u32>(0.625f),
                                                   FlushDenormals | 190U);
} // namespace

TEST_CASE("Synthetic FMUL preserves high guest registers across uniform mirrors",
          "[video_core][ampere][logged_numeric][kirby]") {
    using Shader::Ampere::NativeRegisterCopy;
    constexpr u32 destination = 233, first = 61, second = 174, borrowed = 213;
    constexpr auto multiply = SyntheticInstruction(0x220, destination, first, second,
                                                    FlushDenormals | UnitMultiplyScale);
    const auto pressure = Shader::Ampere::PrepareRegisterPressure(multiply, 216);
    REQUIRE(pressure.valid);
    CHECK(pressure.instruction.Bits(16, 8) == borrowed);
    CHECK((pressure.instruction.words[0] & ~(255U << 16)) ==
          (multiply.words[0] & ~(255U << 16)));
    CHECK(pressure.instruction.words[1] == multiply.words[1]);
    CHECK(pressure.instruction.words[2] == multiply.words[2]);
    CHECK(pressure.instruction.words[3] == multiply.words[3]);
    REQUIRE(pressure.prefix.size() == 8);
    REQUIRE(pressure.suffix.size() == 8);

    const auto lowered = LowerToMaxwell(std::array{UniformBankMarker, multiply, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    REQUIRE(lowered.instructions.size() == 18);
    CHECK(std::equal(pressure.prefix.begin(), pressure.prefix.end(), lowered.instructions.begin()));
    CHECK(std::equal(pressure.suffix.begin(), pressure.suffix.end(), lowered.instructions.begin() + 9));
    CHECK(lowered.instructions[8] ==
          (0x5c68000000070000ULL | borrowed | (u64{first} << 8) | (u64{second} << 20) |
           (1ULL << 44)));
    auto code = lowered.instructions;
    code.insert(code.begin(), 0x0100000000070000ULL | second | (15ULL << 12) |
                                  (u64{std::bit_cast<u32>(3.5f)} << 20));
    code.insert(code.begin(), 0x0100000000070000ULL | first | (15ULL << 12) |
                                  (u64{std::bit_cast<u32>(-2.0f)} << 20));
    code.insert(code.end() - 1, NativeRegisterCopy(0, 0, 1, destination));
    code.insert(code.end() - 1, Store(0));
    TranslateAndEmit(code, std::bit_cast<u32>(-7.0f));

    for (const u32 field : {16U, 24U, 32U}) {
        for (const u32 high : {216U, 217U, 240U, 246U, 254U}) {
            auto instruction = multiply;
            const u32 word = field / 32, shift = field % 32;
            instruction.words[word] =
                (instruction.words[word] & ~(255U << shift)) | (high << shift);
            const auto remapped = Shader::Ampere::PrepareRegisterPressure(instruction, 216);
            REQUIRE(remapped.valid);
            CHECK(remapped.instruction.Bits(field, 8) < 216);
            CHECK(LowerToMaxwell(std::array{UniformBankMarker, instruction, Exit}));
        }
    }
}

TEST_CASE("Synthetic uniform IMAD preserves high destinations across uniform mirrors",
          "[video_core][ampere][logged_numeric][ocarina]") {
    // Independently choose two destination quartets and different lanes in those quartets.
    // IMAD's required predicate fields occupy bits 81..83 (7) and 87..90 (15);
    // bit 91 selects the uniform operand,
    // bit 73 selects signed multiplication, and bits 64..71 name the zero addend.
    constexpr u32 mode = (7U << 17) | (15U << 23) | (1U << 27) | (1U << 9) | 255U;
    constexpr std::array multiplies{
        SyntheticInstruction(0xc24, 232, 41, 9, mode),
        SyntheticInstruction(0xc24, 239, 57, 9, mode),
    };
    const Instruction uniform_marker{{0x0000fc02, 0, 0, 0}};
    for (const auto& multiply : multiplies) {
        const auto pressure = Shader::Ampere::PrepareRegisterPressure(multiply, 216);
        REQUIRE(pressure.valid);
        CHECK(pressure.instruction.Bits(16, 8) == 212 + multiply.Bits(16, 8) % 4);
        CHECK(pressure.instruction.Bits(24, 8) == multiply.Bits(24, 8));
        CHECK(pressure.instruction.words[1] == multiply.words[1]);
        CHECK(pressure.instruction.words[2] == multiply.words[2]);
        REQUIRE(pressure.prefix.size() == 8);
        REQUIRE(pressure.suffix.size() == 8);
        const auto lowered = LowerToMaxwell(std::array{uniform_marker, multiply, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        CHECK(std::equal(pressure.prefix.begin(), pressure.prefix.end(),
                         lowered.instructions.begin()));
    }
}

TEST_CASE("Synthetic uniform SHF preserves high registers across uniform mirrors",
          "[video_core][ampere][logged_numeric][ocarina]") {
    using Shader::Ampere::NativeRegisterCopy;
    // Aliasing low source/destination R201, UR8 amount, RZ high half; .U32 at 73..74,
    // .W at 75, uniform form at 91. A high uniform below forces mirrors down to R200.
    constexpr auto shift = SyntheticInstruction(0xc19, 201, 201, 8,
                                                255U | (3U << 9) | (1U << 11) | (1U << 27), 3);
    const auto pressure = Shader::Ampere::PrepareRegisterPressure(shift, 200);
    REQUIRE(pressure.valid);
    CHECK(pressure.instruction.words[0] == (0xc19U | (3U << 12) | (197U << 16) | (197U << 24)));
    CHECK(pressure.instruction.words[1] == shift.words[1]);
    CHECK(pressure.instruction.words[2] == shift.words[2]);
    CHECK(pressure.instruction.words[3] == shift.words[3]);
    REQUIRE(pressure.prefix.size() == 8);
    REQUIRE(pressure.suffix.size() == 8);

    // MOV R4, UR43: the same mirror boundary, independently selected destination/source.
    constexpr auto high_uniform = SyntheticInstruction(0xc02, 4, 0, 43, (15U << 8) | (1U << 27));
    for (const u32 guard : {3U, 7U}) {
        auto guarded = shift;
        guarded.words[0] = (guarded.words[0] & ~0xf000U) | (guard << 12);
        const auto lowered = LowerToMaxwell(std::array{high_uniform, guarded, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        REQUIRE(lowered.instructions.size() == 19);
        CHECK(std::equal(pressure.prefix.begin(), pressure.prefix.end(),
                         lowered.instructions.begin() + 1));
        CHECK(std::equal(pressure.suffix.begin(), pressure.suffix.end(),
                         lowered.instructions.begin() + 10));
        CHECK(lowered.instructions[9] ==
              (0x5bf8000000000000ULL | (u64{guard} << 16) | 197 | (255ULL << 8) | (224ULL << 20) |
               (197ULL << 39) | (1ULL << 50)));
        if (guard != 7)
            continue;
        for (const u32 amount : {0U, 3U, 31U, 35U}) {
            auto code = lowered.instructions;
            code.insert(code.begin(), NativeRegisterCopy(1, 201, 0, 0));
            code.insert(code.begin(),
                        0x0100000000070000ULL | (15ULL << 12) | (0x9345abcdULL << 20));
            code.insert(code.begin(),
                        0x0100000000070000ULL | 224 | (15ULL << 12) | (u64{amount} << 20));
            code.insert(code.end() - 1, NativeRegisterCopy(0, 0, 1, 201));
            code.insert(code.end() - 1, Store(0));
            CAPTURE(amount);
            TranslateAndEmit(code, 0x9345abcdU << (amount & 31));
        }
    }
}

TEST_CASE("Synthetic global loads preserve high destinations across uniform mirrors",
          "[video_core][ampere][logged_numeric][kirby]") {
    constexpr std::array loads{
        SyntheticLoad(245, 84, 0x74),
        SyntheticLoad(225, 104, 0x4c),
        SyntheticLoad(226, 118, 0xb8),
    };
    for (const auto& load : loads) {
        const auto pressure = Shader::Ampere::PrepareRegisterPressure(load, 216);
        REQUIRE(pressure.valid);
        CHECK(pressure.instruction.Bits(16, 8) == 212 + load.Bits(16, 8) % 4);
        CHECK(pressure.instruction.Bits(24, 8) == load.Bits(24, 8));
        CHECK(pressure.instruction.words[1] == load.words[1]);
        CHECK(pressure.instruction.words[2] == load.words[2]);
        REQUIRE(pressure.prefix.size() == 8);
        REQUIRE(pressure.suffix.size() == 8);
        const auto lowered = LowerToMaxwell(std::array{UniformBankMarker, load, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        REQUIRE(lowered.instructions.size() == 18);
        CHECK(std::equal(pressure.prefix.begin(), pressure.prefix.end(), lowered.instructions.begin()));
        CHECK(std::equal(pressure.suffix.begin(), pressure.suffix.end(), lowered.instructions.begin() + 9));
        CHECK(lowered.instructions[8] ==
              (0xeed0000000070000ULL | pressure.instruction.Bits(16, 8) |
               (u64{load.Bits(24, 8)} << 8) | (u64{load.Bits(40, 24)} << 20) |
               (1ULL << 45) | (4ULL << 48)));
    }
}

TEST_CASE("Synthetic ALU and memory operands preserve encoding fields during shadowing",
          "[video_core][ampere][logged_numeric][kirby]") {
    struct Fixture {
        Instruction instruction;
        std::vector<u32> fields;
    };
    // P sets the comparison result, Q discards its complement, and PT combines with it.
    constexpr u32 float_compare = (12U << 12) | FlushDenormals | (3U << 17) | (7U << 20) |
                                  (7U << 23);
    constexpr u32 uniform_load = (1U << 27) | (15U << 17) | (1U << 15) | (4U << 9) |
                                 (1U << 8);
    constexpr u32 extended_store = (1U << 20) | (1U << 15) | (1U << 13) | (4U << 9) |
                                   (1U << 8);
    const std::array fixtures{
        Fixture{RegisterAdd, {16, 24, 32}},
        Fixture{ImmediateMultiply, {16, 24}},
        Fixture{ImmediateAdd, {16, 24}},
        Fixture{SyntheticInstruction(0x421, 46, 243, std::bit_cast<u32>(-1.75f), FlushDenormals),
                {16, 24}},
        Fixture{SyntheticInstruction(0x919, 250, 0, 0, 34U << 8), {16}}, // S2R thread ID Z.
        Fixture{ImmediateFma, {16, 24, 64}},
        Fixture{SyntheticInstruction(0x80b, 0, 222, std::bit_cast<u32>(6.25f), float_compare), {24}},
        Fixture{RegisterFma, {16, 24, 32, 64}},
        Fixture{SyntheticInstruction(0x208, 57, 57, 228, (4U << 23) | (1U << 26)), {16, 24, 32}},
        // Uniform base plus a register, with an independently chosen byte displacement.
        Fixture{SyntheticInstruction(0x981, 47, 245, (0x148U << 8) | 14U,
                                     uniform_load | (1U << 13), 11), {16, 24}},
        Fixture{SyntheticInstruction(0x981, 237, 237, (0x28U << 8) | 18U, uniform_load), {16, 24}},
        // Lower a signed immediate minimum/maximum in place, then a register comparison with RZ.
        Fixture{SyntheticInstruction(0x817, 235, 235, 19, (15U << 23) | (1U << 9)), {16, 24}},
        Fixture{SyntheticInstruction(0x20b, 0, 247, 255, float_compare, 10), {24, 32}},
        Fixture{SyntheticInstruction(0x245, 58, 0, 220, (1U << 21) | (1U << 12)), {16, 32}},
        Fixture{SyntheticInstruction(0x986, 0, 63, (0x54U << 8) | 242U,
                                     (1U << 27) | extended_store | 10U), {24, 32}},
        Fixture{SyntheticInstruction(0x386, 0, 168, (0x1cU << 8) | 249U, extended_store, 1),
                {24, 32}},
    };
    for (const auto& fixture : fixtures) {
        const auto& instruction = fixture.instruction;
        CAPTURE(instruction.words);
        const auto pressure = Shader::Ampere::PrepareRegisterPressure(instruction, 216);
        REQUIRE(pressure.valid);
        std::array<u32, 4> masks{};
        for (const u32 field : fixture.fields) {
            masks[field / 32] |= 255U << (field % 32);
            const u32 original = instruction.Bits(field, 8);
            if (original >= 216 && original != 255) {
                CHECK(pressure.instruction.Bits(field, 8) < 216);
            } else {
                CHECK(pressure.instruction.Bits(field, 8) == original);
            }
        }
        for (u32 word = 0; word < 4; ++word) {
            CHECK((pressure.instruction.words[word] & ~masks[word]) ==
                  (instruction.words[word] & ~masks[word]));
        }
        const auto lowered = LowerToMaxwell(std::array{UniformBankMarker, instruction, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
    }
}

TEST_CASE("Synthetic shadowed floating ALU operands preserve distinct values",
          "[video_core][ampere][logged_numeric][kirby]") {
    struct Source {
        u32 reg;
        float value;
    };
    struct Fixture {
        Instruction instruction;
        std::vector<Source> sources;
        float expected;
    };
    const std::array fixtures{
        Fixture{RegisterAdd, {{137, 7.0f}, {73, -2.5f}}, 4.5f},
        Fixture{ImmediateMultiply, {{72, 2.0f}}, 47.0f},
        Fixture{ImmediateAdd, {{224, -4.0f}}, -2.75f},
        Fixture{RegisterFma, {{233, 4.0f}, {132, 1.5f}, {51, -0.5f}}, 5.5f},
        Fixture{ImmediateFma, {{241, 8.0f}, {190, 3.0f}}, 8.0f},
    };
    for (const auto& fixture : fixtures) {
        CAPTURE(fixture.instruction.words);
        const auto lowered = LowerToMaxwell(std::array{UniformBankMarker, fixture.instruction, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        std::vector<u64> code;
        for (const auto [reg, value] : fixture.sources) {
            code.push_back(0x0100000000070000ULL | (15ULL << 12) |
                           (u64{std::bit_cast<u32>(value)} << 20));
            code.push_back(Shader::Ampere::NativeRegisterCopy(reg >= 216 ? 1 : 0, reg, 0, 0));
        }
        code.insert(code.end(), lowered.instructions.begin(), lowered.instructions.end() - 1);
        const u32 destination = fixture.instruction.Bits(16, 8);
        code.push_back(Shader::Ampere::NativeRegisterCopy(0, 0, destination >= 216 ? 1 : 0,
                                                       destination));
        code.push_back(Store(0));
        code.push_back(lowered.instructions.back());
        TranslateAndEmit(code, std::bit_cast<u32>(fixture.expected));
    }
}

TEST_CASE("Synthetic masked dependency barrier fences shared memory and rejects unknown modes",
          "[video_core][ampere][logged_numeric]") {
    using Shader::Ampere::Test::DependencyWait;
    using Shader::Ampere::Test::SharedCopy;
    constexpr Instruction wait = DependencyWait(0, 2, 0x21);
    for (const auto supported : {wait, DependencyWait(0, 0, 0)}) {
        const auto ordered = LowerToMaxwell(std::array{supported, Exit});
        REQUIRE(ordered);
        REQUIRE(ordered.instructions.size() == 1);
        CHECK(ordered.instructions.back() == 0xe30000000007000fULL);
        constexpr auto copy = SharedCopy(27, 40, 12, 0x90, 0x40);
        const auto copying = LowerToMaxwell(std::array{copy, supported, Exit});
        REQUIRE(copying);
        CHECK(copying.instructions[copying.source_instruction_offsets[1]] ==
              0xef98000000070000ULL);
        CHECK(copying.source_instruction_offsets[2] == copying.source_instruction_offsets[1] + 1);
    }
    for (u32 bit = 15; bit < 32; ++bit) {
        auto invalid = wait;
        invalid.words[1] ^= 1U << bit;
        const auto rejected = LowerToMaxwell(std::array{invalid, Exit});
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error == "native dependency barrier mode");
        CHECK(rejected.instructions.empty());
        CHECK(rejected.error_offset == 0);
    }
    auto invalid = wait;
    invalid.words[2] = 1;
    const auto rejected = LowerToMaxwell(std::array{invalid, Exit});
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error == "native dependency barrier mode");
}

TEST_CASE("Synthetic mesh attribute stores read shadowed high registers",
          "[video_core][ampere][logged_numeric][oot]") {
    // ISBEWR [R179 + 0x540], R244. Operand fields: data [39:32], offset [63:40],
    // .32 bit 75, .ATTR bits 76..77, .SKEW bit 78, .O bit 79. R244 overlaps UR26's mirror.
    const Instruction uniform_marker{{0x0000fc02, 0, 0, 0}};
    constexpr auto store = SyntheticInstruction(0x927, 0, 179, 244U | (0x540U << 8),
                                                (1U << 11) | (3U << 12) | (1U << 14) | (1U << 15));
    const auto lowered = LowerToMaxwell(std::array{uniform_marker, store, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    const auto begin = lowered.instructions.begin() + lowered.source_instruction_offsets[1];
    const auto end = lowered.instructions.begin() + lowered.source_instruction_offsets[2];
    const auto pseudo = std::find_if(begin, end, [](u64 word) { return (word >> 52) == 0xefc; });
    REQUIRE(pseudo != end);
    const u32 data = static_cast<u32>(*pseudo & 0xff);
    CHECK(data < 216);
    CHECK(std::find(begin, pseudo, Shader::Ampere::NativeRegisterCopy(0, data, 1, 244)) != pseudo);
    CHECK(((*pseudo >> 8) & 255) == 179);
    CHECK(((*pseudo >> 20) & 0xffffff) == 0x540);
}

TEST_CASE("Global atomics read their data from shadowed high registers",
          "[video_core][ampere][logged_numeric]") {
    // ATOMG.E.ADD.STRONG.GPU R4, [UR4], R240. R240 is UR22's mirror once the uniform bank is in
    // use, so the data word must come from the shadow bank.
    const Instruction uniform_marker{{0x0000fc02, 0, 0, 0}};
    const Instruction atomic{{0xff0479a8, 0x000000f0, 0x081ee144, 0x000fe200}};
    const auto lowered = LowerToMaxwell(std::array{uniform_marker, atomic, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    const auto begin = lowered.instructions.begin() + lowered.source_instruction_offsets[1];
    const auto end = lowered.instructions.begin() + lowered.source_instruction_offsets[2];
    const auto native = std::find_if(begin, end, [](u64 word) { return (word >> 56) == 0xed; });
    REQUIRE(native != end);
    const u32 data = static_cast<u32>((*native >> 20) & 0xff);
    CHECK(data < 216);
    CHECK(std::find(begin, native, Shader::Ampere::NativeRegisterCopy(0, data, 1, 240)) != native);
}

TEST_CASE("Gradient texture fetches read their gradients from shadowed high registers",
          "[video_core][ampere][logged_numeric]") {
    // TXD.2D R4, R8, R240 with two channels. The four gradients R240-R243 alias UR22-UR25's
    // mirrors once the uniform bank is in use, so all four must come from the shadow bank.
    const Instruction uniform_marker{{0x0000fc02, 0, 0, 0}};
    const Instruction gradient{{0x08047b6c, 0x200000f0, 0x001e03ff, 0x000fe200}};
    const auto lowered = LowerToMaxwell(std::array{uniform_marker, gradient, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    const auto begin = lowered.instructions.begin() + lowered.source_instruction_offsets[1];
    const auto end = lowered.instructions.begin() + lowered.source_instruction_offsets[2];
    const auto native = std::find_if(begin, end, [](u64 word) { return (word >> 56) == 0xde; });
    REQUIRE(native != end);
    const u32 gradients = static_cast<u32>((*native >> 20) & 0xff);
    CHECK(gradients < 216);
    for (u32 i = 0; i < 4; ++i)
        CHECK(std::find(begin, native,
                        Shader::Ampere::NativeRegisterCopy(0, gradients + i, 1, 240 + i)) != native);
}

TEST_CASE("Gradient texture fetch shadows keep the array layer register",
          "[video_core][ampere][logged_numeric]") {
    // TXD.ARRAY_2D R4, R210, R240: the coordinates are R210, R211 and the layer in R212. The
    // gradients' shadow must not borrow the R212-R215 quartet, or its copies overwrite the layer.
    const Instruction uniform_marker{{0x0000fc02, 0, 0, 0}};
    const Instruction gradient{{0xd2047b6c, 0xa00000f0, 0x001e03ff, 0x000fe200}};
    const auto lowered = LowerToMaxwell(std::array{uniform_marker, gradient, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    const auto begin = lowered.instructions.begin() + lowered.source_instruction_offsets[1];
    const auto end = lowered.instructions.begin() + lowered.source_instruction_offsets[2];
    const auto native = std::find_if(begin, end, [](u64 word) { return (word >> 56) == 0xde; });
    REQUIRE(native != end);
    CHECK(((*native >> 8) & 0xff) == 210);
    const u32 gradients = static_cast<u32>((*native >> 20) & 0xff);
    CHECK((gradients + 4 <= 210 || gradients >= 213));
}

TEST_CASE("Synthetic high-register operands preserve encoding fields",
          "[video_core][ampere][logged_numeric][ocarina]") {
    // Independently assembled coverage of each operand location and register-pair boundary.
    // No game program or scheduling word is used. Mode fields follow the corresponding
    // opcode decoder, while register numbers and payloads are arbitrary test inputs.
    struct Fixture {
        Instruction instruction;
        std::vector<u32> fields;
    };
    const std::array fixtures{
        // R2UR: destination is uniform, source ordinary; PT field at 81..83.
        Fixture{SyntheticInstruction(0x3c2, 34, 245, 0, 7U << 17), {24}},
        Fixture{SyntheticInstruction(0x3c2, 32, 218, 0, 7U << 17), {24}},
        Fixture{SyntheticInstruction(0x3c2, 9, 231, 0, 7U << 17), {24}},
        // FMNMX immediate 0.125: FTZ at 80, PT at 87..89, negate predicate at 90 (max).
        Fixture{SyntheticInstruction(0x809, 230, 230, std::bit_cast<u32>(0.125f),
                                     (1U << 16) | (7U << 23) | (1U << 26)), {16, 24}},
        Fixture{SyntheticInstruction(0x809, 241, 241, std::bit_cast<u32>(0.125f),
                                     (1U << 16) | (7U << 23) | (1U << 26)), {16, 24}},
        Fixture{SyntheticInstruction(0x809, 223, 223, std::bit_cast<u32>(0.125f),
                                     (1U << 16) | (7U << 23) | (1U << 26)), {16, 24}},
        // @!P3 FMUL R234, R234, UR37: FTZ at 80, unit scale at 84..86, uniform at 91.
        Fixture{SyntheticInstruction(0xc20, 234, 234, 37,
                                     (1U << 16) | (4U << 20) | (1U << 27), 11), {16, 24}},
        // P2R from RZ with masks selecting distinct predicate subsets.
        Fixture{SyntheticInstruction(0x803, 225, 255, 0x12, 0), {16, 24}},
        Fixture{SyntheticInstruction(0x803, 226, 255, 0x24, 0), {16, 24}},
        // I2FP: F32 at 74..75, U32 at 76..77, uniform-source field at 85/91.
        Fixture{SyntheticInstruction(0xc45, 248, 0, 13,
                                     (1U << 10) | (1U << 12) | (1U << 21) | (1U << 27)), {16}},
        // LDC.64 R208/R209, indexed by R229; bank 3 and byte offset 0x180.
        Fixture{SyntheticInstruction(0xb82, 208, 229, (3U << 22) | 0x180U, 5U << 9), {16, 24}},
        // FFMA R45, R31, UR7, R235: addend occupies bits 64..71; FTZ at 80.
        Fixture{SyntheticInstruction(0xc23, 45, 31, 7, 235U | (1U << 16) | (1U << 27)), {16, 24, 64}},
    };
    for (const auto& fixture : fixtures) {
        const auto& instruction = fixture.instruction;
        CAPTURE(instruction.words);
        const auto pressure = Shader::Ampere::PrepareRegisterPressure(instruction, 216);
        REQUIRE(pressure.valid);
        CHECK(pressure.prefix.size() == 8);
        CHECK(pressure.suffix.size() == 8);
        std::array<u32, 4> masks{};
        for (const u32 field : fixture.fields) {
            masks[field / 32] |= 255U << (field % 32);
            const u32 original = instruction.Bits(field, 8);
            if (original >= 216 && original != 255) {
                CHECK(pressure.instruction.Bits(field, 8) == 212 + original % 4);
            } else {
                CHECK(pressure.instruction.Bits(field, 8) == original);
            }
        }
        for (u32 word = 0; word < 4; ++word) {
            CHECK((pressure.instruction.words[word] & ~masks[word]) ==
                  (instruction.words[word] & ~masks[word]));
        }
        const Instruction uniform_marker{{0x0000fc02, 0, 0, 0}};
        const auto lowered = LowerToMaxwell(std::array{uniform_marker, instruction, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
    }

    // With mirrors at R204, R208/R209 is shadowed too and must remain a consecutive pair.
    constexpr auto load = SyntheticInstruction(0xb82, 208, 229, (3U << 22) | 0x180U, 5U << 9);
    const auto pressure = Shader::Ampere::PrepareRegisterPressure(load, 204);
    REQUIRE(pressure.valid);
    CHECK(pressure.instruction.Bits(16, 8) == 200);
    CHECK(pressure.instruction.Bits(24, 8) == 197);
    CHECK(pressure.prefix.size() == 16);
    CHECK(pressure.suffix.size() == 16);
}

TEST_CASE("Synthetic shadowed floating arithmetic preserves values",
          "[video_core][ampere][logged_numeric][ocarina]") {
    struct Source {
        u32 bank, reg;
    };
    struct Fixture {
        Instruction instruction;
        std::vector<Source> sources;
        float expected;
    };
    // Every source holds 2.0. Uniform mirrors: UR7 at R223, UR37 at R213 (base R204).
    // Expected values come from scalar max/multiply/fused multiply-add, not emitted words.
    const std::array fixtures{
        Fixture{SyntheticInstruction(0x809, 230, 230, std::bit_cast<u32>(0.125f),
                                     (1U << 16) | (7U << 23) | (1U << 26)), {{1, 230}}, 2.0f},
        Fixture{SyntheticInstruction(0xc20, 234, 234, 37, (1U << 16) | (4U << 20) | (1U << 27)),
                {{1, 234}, {0, 213}}, 4.0f},
        Fixture{SyntheticInstruction(0xc23, 45, 31, 7, 235U | (1U << 16) | (1U << 27)),
                {{0, 31}, {0, 223}, {1, 235}}, 6.0f},
    };
    for (const auto& fixture : fixtures) {
        CAPTURE(fixture.instruction.words);
        const Instruction uniform_marker{{0x0000fc02, 0, 0, 0}};
        const auto lowered = LowerToMaxwell(std::array{uniform_marker, fixture.instruction, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        std::vector<u64> code{0x0100000000070000ULL | (15ULL << 12) | (0x40000000ULL << 20)};
        for (const auto [bank, reg] : fixture.sources) {
            code.push_back(Shader::Ampere::NativeRegisterCopy(bank, reg, 0, 0));
        }
        code.insert(code.end(), lowered.instructions.begin(), lowered.instructions.end() - 1);
        const u32 destination = fixture.instruction.Bits(16, 8);
        code.push_back(
            Shader::Ampere::NativeRegisterCopy(0, 0, destination >= 216 ? 1 : 0, destination));
        code.push_back(Store(0));
        code.push_back(lowered.instructions.back());
        TranslateAndEmit(code, std::bit_cast<u32>(fixture.expected));
    }
}
