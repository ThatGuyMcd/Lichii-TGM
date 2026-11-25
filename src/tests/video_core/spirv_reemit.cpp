// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <spirv/unified1/spirv.hpp11>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"

namespace {
using namespace Shader;

struct ComputeProgram {
    ObjectPool<IR::Inst> pool{256};
    IR::Block block{pool};
    IR::IREmitter ir{block};
    IR::Program program;
    Profile profile{};

    ComputeProgram() {
        program.stage = Stage::Compute;
        program.workgroup_size = {32, 1, 1};
        program.blocks = {&block};
        program.post_order_blocks = {&block};
        program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                               {{}, IR::AbstractSyntaxNode::Type::Return}};
        program.info.uses_local_invocation_id = true;
        program.info.used_storage_buffer_types = IR::Type::U32;
        program.info.storage_buffers_descriptors.push_back({.count = 1, .is_written = true});
        profile.supported_spirv = 0x00010400;
        profile.min_ssbo_alignment = 16;
        profile.unified_descriptor_binding = true;
    }

    void Store(const IR::U32& value, u32 byte_offset = 0) {
        block.AppendNewInst(IR::Opcode::WriteStorage32,
                            {IR::Value{0U}, IR::Value{byte_offset}, value});
    }
};

struct InstructionState {
    IR::Opcode opcode;
    u32 flags;
    int uses;
    std::vector<IR::Value> arguments;

    bool operator==(const InstructionState&) const = default;
};

std::vector<InstructionState> Snapshot(const IR::Block& block) {
    std::vector<InstructionState> result;
    for (const IR::Inst& inst : block.Instructions()) {
        InstructionState state{inst.GetOpcode(), inst.Flags<u32>(), inst.UseCount(), {}};
        for (size_t arg = 0; arg < inst.NumArgs(); ++arg) {
            state.arguments.push_back(inst.Arg(arg));
        }
        result.push_back(std::move(state));
    }
    return result;
}

void DumpFirstModule(std::string_view name, std::span<const u32> code) {
    if (const char* directory = std::getenv("EDEN_SPIRV_REEMIT_TEST_OUTPUT")) {
        std::filesystem::create_directories(directory);
        const auto path = std::filesystem::path{directory} / (std::string{name} + ".spv");
        std::ofstream file{path, std::ios::binary};
        file.write(reinterpret_cast<const char*>(code.data()),
                   static_cast<std::streamsize>(code.size_bytes()));
        REQUIRE(file.good());
    }
}

size_t CountOpcode(std::span<const u32> code, spv::Op opcode) {
    size_t found{};
    for (size_t offset = 5; offset < code.size();) {
        const u32 count = code[offset] >> 16;
        REQUIRE(count != 0);
        REQUIRE(count <= code.size() - offset);
        found += static_cast<spv::Op>(code[offset] & 0xffffU) == opcode;
        offset += count;
    }
    return found;
}

void CheckRepeatedEmission(ComputeProgram& fixture, std::string_view name,
                           std::span<const spv::Op> required_opcodes) {
    CAPTURE(name);
    const auto original = Snapshot(fixture.block);
    const auto first = Backend::SPIRV::EmitSPIRV(fixture.profile, fixture.program);
    DumpFirstModule(name, first);
    const auto after_first = Snapshot(fixture.block);
    for (spv::Op opcode : required_opcodes) {
        CAPTURE(static_cast<u32>(opcode));
        CHECK(CountOpcode(first, opcode) != 0);
    }
    const auto second = Backend::SPIRV::EmitSPIRV(fixture.profile, fixture.program);
    CHECK(first == second);
    CHECK(after_first == original);
    CHECK(Snapshot(fixture.block) == original);
}
} // namespace

TEST_CASE("Repeated SPIR-V emission preserves all integer flag forms", "[spirv_reemit]") {
    struct FlagCase {
        IR::Opcode flag;
        spv::Op spirv_opcode;
        std::string_view name;
    };
    constexpr std::array flags{
        FlagCase{IR::Opcode::GetZeroFromOp, spv::Op::OpIEqual, "zero"},
        FlagCase{IR::Opcode::GetSignFromOp, spv::Op::OpSLessThan, "sign"},
        FlagCase{IR::Opcode::GetCarryFromOp, spv::Op::OpIAddCarry, "carry"},
        FlagCase{IR::Opcode::GetOverflowFromOp, spv::Op::OpSGreaterThanEqual, "overflow"},
    };
    for (const auto& [flag_opcode, spirv_opcode, name] : flags) {
        ComputeProgram fixture;
        auto& ir = fixture.ir;
        const auto sum = ir.IAdd(ir.LocalInvocationIdX(), ir.Imm32(0x8b32e485U));
        fixture.block.AppendNewInst(flag_opcode, {sum});
        const IR::U1 flag{&fixture.block.back()};
        fixture.Store(IR::U32{ir.Select(flag, ir.Imm32(19), sum)});
        CheckRepeatedEmission(fixture, name, std::array{spirv_opcode});
    }

    ComputeProgram fixture;
    auto& ir = fixture.ir;
    const auto sum = ir.IAdd(ir.LocalInvocationIdX(), ir.Imm32(0xd036a51bU));
    for (size_t index = 0; index < flags.size(); ++index) {
        fixture.block.AppendNewInst(flags[index].flag, {sum});
        const IR::U1 flag{&fixture.block.back()};
        const auto byte_offset = static_cast<u32>(index * sizeof(u32));
        fixture.Store(IR::U32{ir.Select(flag, ir.Imm32(37), sum)}, byte_offset);
    }
    CheckRepeatedEmission(fixture, "integer-flags",
                          std::array{spv::Op::OpIEqual, spv::Op::OpSLessThan, spv::Op::OpIAddCarry,
                                     spv::Op::OpSGreaterThanEqual});
}

TEST_CASE("Repeated SPIR-V emission preserves sparse fetch residency", "[spirv_reemit]") {
    ComputeProgram fixture;
    auto& ir = fixture.ir;
    fixture.program.info.uses_sparse_residency = true;
    fixture.program.info.texture_descriptors.push_back({.type = TextureType::Color2D, .count = 1});
    IR::TextureInstInfo info{};
    info.type.Assign(TextureType::Color2D);
    const auto coords = ir.CompositeConstruct(ir.LocalInvocationIdX(), ir.Imm32(9));
    const auto texel = ir.ImageFetch(ir.Imm32(0), coords, {}, ir.Imm32(0), {}, info);
    texel.InstRecursive()->ReplaceOpcode(IR::Opcode::ImageFetch);
    const auto resident = ir.GetSparseFromOp(texel);
    const auto value = ir.BitCast<IR::U32>(IR::F32{ir.CompositeExtract(texel, 2)});
    fixture.Store(IR::U32{ir.Select(resident, value, ir.Imm32(71))});
    CheckRepeatedEmission(fixture, "sparse",
                          std::array{spv::Op::OpImageSparseFetch,
                                     spv::Op::OpImageSparseTexelsResident});
}

TEST_CASE("Repeated SPIR-V emission preserves shuffle bounds", "[spirv_reemit]") {
    ComputeProgram fixture;
    auto& ir = fixture.ir;
    fixture.program.info.uses_subgroup_shuffles = true;
    fixture.profile.support_vote = true;
    fixture.profile.max_subgroup_size = 32;
    const auto lane = ir.LocalInvocationIdX();
    const auto input = ir.IAdd(lane, ir.Imm32(29));
    const auto shuffled = ir.ShuffleIndex(input, lane, ir.Imm32(23), ir.Imm32(0));
    const auto in_bounds = ir.GetInBoundsFromOp(shuffled);
    fixture.Store(IR::U32{ir.Select(in_bounds, shuffled, ir.Imm32(71))});
    CheckRepeatedEmission(fixture, "shuffle",
                          std::array{spv::Op::OpGroupNonUniformShuffle,
                                     spv::Op::OpSLessThanEqual});
}

TEST_CASE("SPIR-V emission rejects pseudo-ops without a current parent definition",
          "[spirv_reemit]") {
    using Catch::Matchers::ContainsSubstring;
    ComputeProgram fixture;
    auto& ir = fixture.ir;
    const auto bad_flag = ir.GetCarryFromOp(ir.LocalInvocationIdX());
    bad_flag.InstRecursive()->SetDefinition(83U);
    fixture.Store(IR::U32{ir.Select(bad_flag, ir.Imm32(1), ir.Imm32(2))});
    CHECK_THROWS_WITH(Backend::SPIRV::EmitSPIRV(fixture.profile, fixture.program),
                      ContainsSubstring("GetCarryFromOp was not defined by its parent "));

    ComputeProgram no_parent;
    no_parent.block.AppendNewInst(IR::Opcode::GetCarryFromOp, {IR::Value{9U}});
    CHECK_THROWS_WITH(Backend::SPIRV::EmitSPIRV(no_parent.profile, no_parent.program),
                      ContainsSubstring("GetCarryFromOp was not defined by its parent "
                                        "(not an instruction)"));
}
