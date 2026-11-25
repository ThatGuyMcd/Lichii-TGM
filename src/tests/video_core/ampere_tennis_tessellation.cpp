// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <algorithm>
#include <span>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/maxwell/structured_control_flow.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
using Shader::Ampere::TessellationStage;
namespace IR = Shader::IR;

constexpr Instruction Exit{{0x0000794d, 0, 0x03800000, 0}};
constexpr u32 NoRegister = 255;
constexpr u32 InvocationId = 17;
constexpr u32 InvocationInfo = 29;

constexpr Instruction ReadSpecial(u32 destination, u32 special) {
    return {{0x919 | (7U << 12) | (destination << 16), 0, special << 8, 0}};
}

constexpr Instruction ReadVertexHandle(u32 destination, u32 vertex) {
    return {{0x923 | (7U << 12) | (destination << 16) | (vertex << 24), 0, 0, 0}};
}

constexpr Instruction ReadAttribute(u32 destination, u32 address, bool patch, bool output,
                                    u32 components = 1) {
    return {{0x321 | (7U << 12) | (destination << 16) | (NoRegister << 24),
             NoRegister | (address << 8),
             ((components - 1) << 10) | (u32{patch} << 12) | (u32{output} << 15), 0}};
}

constexpr Instruction WriteAttribute(u32 first, u32 address, bool patch, u32 components = 1) {
    return {{0x322 | (7U << 12) | (NoRegister << 24), first | (address << 8),
             NoRegister | ((components - 1) << 10) | (u32{patch} << 12), 0}};
}

// Exercise independent patch outputs, vertex handles, invocation registers and both evaluation
// coordinates. Position and varying writes keep these values live through IR optimisation.
constexpr std::array ControlProgram{
    ReadSpecial(43, InvocationId),
    ReadSpecial(44, InvocationInfo),
    ReadVertexHandle(45, 43),
    ReadVertexHandle(46, 44),
    ReadVertexHandle(47, 45),
    WriteAttribute(43, 0x00, true),
    WriteAttribute(44, 0x04, true),
    WriteAttribute(45, 0x08, true),
    WriteAttribute(46, 0x0c, true),
    WriteAttribute(47, 0x10, true),
    WriteAttribute(43, 0x14, true),
    WriteAttribute(44, 0x18, true),
    ReadAttribute(54, 0x18, true, true),
    WriteAttribute(54, 0x18, true),
    Exit,
};

constexpr std::array EvaluationProgram{
    ReadAttribute(57, 0x2f0, false, true, 2),
    ReadAttribute(59, 0x18, true, false),
    ReadSpecial(60, InvocationInfo),
    WriteAttribute(57, 0x70, false, 4),
    WriteAttribute(59, 0x80, false),
    Exit,
};

class TessellationEnvironment final : public Shader::Environment {
public:
    TessellationEnvironment(std::span<const u64> code_, Shader::Stage stage_) : code{code_} {
        stage = stage_;
        is_proprietary_driver = true;
        sph.common0.shader_type.Assign(stage == Shader::Stage::TessellationEval ? 3 : 2);
        sph.common2.threads_per_input_primitive.Assign(4);
    }
    u64 ReadInstruction(u32 address) override {
        for (u32 index = 0; index < code.size(); ++index) {
            if (address == 0x50 + index * 8 + ((index + 1) / 3) * 8) {
                return code[index];
            }
        }
        FAIL("Read outside synthetic tessellation program at " << address);
        return 0;
    }
    u32 ReadCbufValue(u32 bank, u32 offset) override { return (bank << 16) | offset; }
    Shader::TextureType ReadTextureType(u32) override { return Shader::TextureType::Color2D; }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return Shader::TexturePixelFormat::A8B8G8R8_UNORM;
    }
    bool IsTexturePixelFormatInteger(u32) override { return false; }
    u32 ReadViewportTransformState() override { return 1; }
    u32 TextureBoundBuffer() const override { return 15; }
    u32 LocalMemorySize() const override { return 0; }
    u32 SharedMemorySize() const override { return 0; }
    std::array<u32, 3> WorkgroupSize() const override { return {1, 1, 1}; }
    bool HasHLEMacroState() const override { return false; }
    bool HasNativeInstructions() const override { return true; }
    bool IsNativeProgram() const override { return true; }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}

private:
    std::span<const u64> code;
};
} // namespace

TEST_CASE("Ampere control shader preserves all tessellation patch outputs",
          "[video_core][ampere][tessellation]") {
    const auto lowered = LowerToMaxwell(ControlProgram, false, true, {}, TessellationStage::Control);
    INFO(lowered.error << " at " << lowered.error_offset);
    REQUIRE(lowered);
    REQUIRE(lowered.source_size == sizeof(ControlProgram));
    for (const size_t index : {2U, 3U, 4U}) {
        const auto& source = ControlProgram[index];
        const u64 word = lowered.instructions[lowered.source_instruction_offsets[index]];
        REQUIRE((word >> 48) == 0x5c98);
        REQUIRE((word & 0xff) == source.Bits(16, 8));
        REQUIRE(((word >> 20) & 0xff) == source.Bits(24, 8));
    }
    for (size_t index = 5; index <= 13; ++index) {
        const auto& source = ControlProgram[index];
        const u64 word = lowered.instructions[lowered.source_instruction_offsets[index]];
        REQUIRE(((word >> 31) & 1) == 1);
        REQUIRE(((word >> 20) & 0x3ff) == source.Bits(40, 10));
        REQUIRE(((word >> 32) & 1) == source.Bits(79, 1));
    }

    TessellationEnvironment env{lowered.instructions, Shader::Stage::TessellationControl};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, Shader::Maxwell::Location{0x50}};
    Shader::ObjectPool<IR::Inst> inst_pool;
    Shader::ObjectPool<IR::Block> block_pool;
    auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg,
                                                     Shader::HostTranslateInfo{});
    REQUIRE(program.invocations == 4);
    REQUIRE(program.info.stores_tess_level_outer);
    REQUIRE(program.info.stores_tess_level_inner);
    REQUIRE(program.info.uses_patches[0]);
    REQUIRE(program.info.uses_invocation_id);
    REQUIRE(program.info.uses_invocation_info);
    std::array<bool, 7> patches{};
    for (const auto* block : program.blocks) {
        for (const auto& inst : *block) {
            if (inst.GetOpcode() == IR::Opcode::SetPatch) {
                const auto patch = static_cast<size_t>(inst.Arg(0).Patch());
                REQUIRE(patch < patches.size());
                patches[patch] = true;
            }
        }
    }
    REQUIRE(std::all_of(patches.begin(), patches.end(), [](bool written) { return written; }));
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010300;
    const auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
    REQUIRE(spirv.size() > 5);
    REQUIRE(spirv[0] == 0x07230203);
}

TEST_CASE("Native tessellation patch loads select the correct input or output namespace",
          "[video_core][ampere][tessellation]") {
    const auto control_load = ReadAttribute(35, 0x18, true, true);
    const auto eval_load = ReadAttribute(36, 0x18, true, false);
    const auto store = WriteAttribute(18, 0x18, true);
    REQUIRE(LowerToMaxwell(std::array{control_load, Exit}, false, false, {}, TessellationStage::Control));
    REQUIRE(LowerToMaxwell(std::array{eval_load, Exit}, false, false, {}, TessellationStage::Evaluation));
    REQUIRE(LowerToMaxwell(std::array{store, Exit}, false, false, {}, TessellationStage::Control));
    REQUIRE_FALSE(LowerToMaxwell(std::array{control_load, Exit}, false, false, {}, TessellationStage::Evaluation));
    REQUIRE_FALSE(LowerToMaxwell(std::array{eval_load, Exit}, false, false, {}, TessellationStage::Control));
    REQUIRE_FALSE(LowerToMaxwell(std::array{store, Exit}, false, false, {}, TessellationStage::Evaluation));
    for (const auto instruction : {control_load, eval_load, store}) {
        REQUIRE_FALSE(LowerToMaxwell(std::array{instruction, Exit}));
        REQUIRE_FALSE(LowerToMaxwell(std::array{instruction, Exit}, true));
    }
    auto physical = control_load;
    physical.words[2] |= 1U << 13;
    REQUIRE_FALSE(LowerToMaxwell(std::array{physical, Exit}, false, false, {}, TessellationStage::Control));
    auto vertex_output = control_load;
    vertex_output.words[2] &= ~(1U << 12);
    REQUIRE_FALSE(LowerToMaxwell(std::array{vertex_output, Exit}, false, false, {}, TessellationStage::Control));
}

TEST_CASE("Native tessellation evaluation patch inputs reach SPIRV",
          "[video_core][ampere][tessellation]") {
    const std::array code{
        ReadAttribute(37, 0x18, true, false),
        WriteAttribute(37, 0x70, false),
        Exit,
    };
    const auto lowered = LowerToMaxwell(code, false, false, {}, TessellationStage::Evaluation);
    INFO(lowered.error);
    REQUIRE(lowered);
    TessellationEnvironment env{lowered.instructions, Shader::Stage::TessellationEval};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, Shader::Maxwell::Location{0x50}};
    Shader::ObjectPool<IR::Inst> inst_pool;
    Shader::ObjectPool<IR::Block> block_pool;
    auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg,
                                                     Shader::HostTranslateInfo{});
    REQUIRE(program.info.uses_patches[0]);
    REQUIRE_FALSE(program.info.stores_tess_level_outer);
    REQUIRE_FALSE(program.info.stores_tess_level_inner);
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010300;
    const auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
    REQUIRE(spirv.size() > 5);
    REQUIRE(spirv[0] == 0x07230203);
}

TEST_CASE("Native tessellation vertex handles retain only supported ISBERD map reads",
          "[video_core][ampere][tessellation]") {
    constexpr u32 destination = 23;
    constexpr u32 source = 19;
    const auto read = ReadVertexHandle(destination, source);
    for (const auto stage : {TessellationStage::Control, TessellationStage::Evaluation}) {
        const auto lowered = LowerToMaxwell(std::array{read, Exit}, false, false, {}, stage);
        REQUIRE(lowered);
        REQUIRE(lowered.instructions[0] ==
                (0x5c98000000000000ULL | (15ULL << 39) | (u64{source} << 20) |
                 (7ULL << 16) | destination));
        auto invalid_offset = read;
        invalid_offset.words[1] |= 1U << 8;
        auto invalid_output = read;
        invalid_output.words[2] |= 1U << 15;
        auto invalid_skew = read;
        invalid_skew.words[2] |= 1U << 14;
        auto invalid_opcode = read;
        invalid_opcode.words[0] ^= 0x923 ^ 0x927;
        for (const auto invalid : {invalid_offset, invalid_output, invalid_skew, invalid_opcode}) {
            REQUIRE_FALSE(LowerToMaxwell(std::array{invalid, Exit}, false, false, {}, stage));
        }
    }
}

TEST_CASE("Ampere evaluation shader reads both tessellation coordinates",
          "[video_core][ampere][tessellation][tennis_evaluation]") {
    const auto lowered =
        LowerToMaxwell(EvaluationProgram, false, true, {}, TessellationStage::Evaluation);
    INFO(lowered.error << " at " << lowered.error_offset);
    REQUIRE(lowered);
    REQUIRE(lowered.source_size == sizeof(EvaluationProgram));
    const u64 coordinates = lowered.instructions[lowered.source_instruction_offsets[0]];
    REQUIRE(((coordinates >> 20) & 0x3ff) == 0x2f0);
    REQUIRE(((coordinates >> 31) & 1) == 0);
    REQUIRE(((coordinates >> 32) & 1) == 1);
    REQUIRE(((coordinates >> 47) & 3) == 1);
    TessellationEnvironment env{lowered.instructions, Shader::Stage::TessellationEval};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, Shader::Maxwell::Location{0x50}};
    Shader::ObjectPool<IR::Inst> inst_pool;
    Shader::ObjectPool<IR::Block> block_pool;
    auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg,
                                                     Shader::HostTranslateInfo{});
    REQUIRE(program.info.loads[IR::Attribute::TessellationEvaluationPointU]);
    REQUIRE(program.info.loads[IR::Attribute::TessellationEvaluationPointV]);
    REQUIRE(program.info.stores.AllComponents(IR::Attribute::PositionX));
    REQUIRE(program.info.stores[IR::Attribute::Generic0X]);
    REQUIRE(program.info.uses_patches[0]);
    REQUIRE(program.info.uses_invocation_info);
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010300;
    Shader::RuntimeInfo runtime{};
    runtime.tess_primitive = Shader::TessPrimitive::Quads;
    Shader::Backend::Bindings bindings{};
    const auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
    REQUIRE(spirv.size() > 5);
    REQUIRE(spirv[0] == 0x07230203);
}

TEST_CASE("Tessellation output-coordinate loads remain confined to evaluation U and V",
          "[video_core][ampere][tessellation][tennis_evaluation]") {
    const auto coordinates = ReadAttribute(47, 0x2f0, false, true, 2);
    for (const auto stage : {TessellationStage::None, TessellationStage::Control}) {
        REQUIRE_FALSE(LowerToMaxwell(std::array{coordinates, Exit}, false, false, {}, stage));
    }
    REQUIRE_FALSE(LowerToMaxwell(std::array{coordinates, Exit}, true));
    auto physical = coordinates;
    physical.words[2] |= 1U << 13;
    auto indexed = coordinates;
    indexed.words[0] = (indexed.words[0] & 0x00ffffffU) | (19U << 24);
    for (const auto invalid : {ReadAttribute(47, 0x2f8, false, true),
                               ReadAttribute(47, 0x2f0, false, true, 3),
                               ReadAttribute(47, 0x2f4, false, true, 2),
                               physical, indexed, ReadAttribute(47, 0x70, false, true, 2)}) {
        REQUIRE_FALSE(LowerToMaxwell(std::array{invalid, Exit}, false, false, {}, TessellationStage::Evaluation));
    }
    const auto v = ReadAttribute(51, 0x2f4, false, true);
    REQUIRE(LowerToMaxwell(std::array{v, Exit}, false, false, {}, TessellationStage::Evaluation));
}
