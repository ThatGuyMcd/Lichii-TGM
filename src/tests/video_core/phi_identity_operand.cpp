// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <spirv/unified1/spirv.hpp11>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/ir_opt/passes.h"

TEST_CASE("Deferred SPIR-V phi emission keeps immediate operands defined",
          "[video_core][spirv][spirv_phi]") {
    using namespace Shader;
    using Node = IR::AbstractSyntaxNode::Type;

    const auto mode = GENERATE(std::string_view{"append"}, std::string_view{"replace"},
                               std::string_view{"undefined"});
    CAPTURE(mode);

    ObjectPool<IR::Inst> pool{32};
    IR::Block entry{pool}, taken{pool}, merge{pool};
    entry.AddBranch(&taken);
    entry.AddBranch(&merge);
    taken.AddBranch(&merge);

    IR::IREmitter start{entry}, branch{taken};
    const auto invocation = start.LocalInvocationIdX();
    const auto condition = start.ConditionRef(start.IEqual(invocation, start.Imm32(3U)));
    constexpr u32 immediate = 73;
    entry.AppendNewInst(IR::Opcode::Identity, {start.Imm32(immediate)});
    entry.AppendNewInst(IR::Opcode::Identity, {IR::Value{&entry.back()}});
    const IR::Value identity{&entry.back()};
    const auto incremented = branch.IAdd(invocation, branch.Imm32(17U));

    merge.AppendNewInst(IR::Opcode::Phi, {});
    auto& phi = merge.back();
    phi.SetFlags(IR::Type::U32);
    if (mode == "replace") {
        phi.AddPhiOperand(&entry, invocation);
        phi.SetArg(0, identity);
        CHECK(invocation.Inst()->UseCount() == 2);
    } else {
        phi.AddPhiOperand(&entry, mode == "append" ? identity : IR::Value{});
    }
    phi.AddPhiOperand(&taken, incremented);
    merge.AppendNewInst(IR::Opcode::WriteStorage32,
                        {IR::Value{0U}, IR::Value{0U}, IR::Value{&phi}});

    IR::Program program;
    program.stage = Stage::Compute;
    program.workgroup_size = {8, 1, 1};
    program.blocks = {&entry, &taken, &merge};
    program.post_order_blocks = {&merge, &taken, &entry};
    program.syntax_list = {{{.block = &entry}, Node::Block},
                           {{.if_node{condition, &taken, &merge}}, Node::If},
                           {{.block = &taken}, Node::Block},
                           {{.end_if{&merge}}, Node::EndIf},
                           {{.block = &merge}, Node::Block},
                           {{}, Node::Return}};
    program.info.uses_local_invocation_id = true;
    program.info.used_storage_buffer_types = IR::Type::U32;
    program.info.storage_buffers_descriptors.push_back({.count = 1, .is_written = true});

    Optimization::DeadCodeEliminationPass(program);
    const Profile profile{
        .supported_spirv = 0x00010300,
        .unified_descriptor_binding = true,
        .min_ssbo_alignment = 16,
    };
    if (mode == "undefined") {
        REQUIRE_THROWS_MATCHES(Backend::SPIRV::EmitSPIRV(profile, program), LogicError,
                               Catch::Matchers::Message("Phi operand 0 has no definition"));
        return;
    }
    const auto code = Backend::SPIRV::EmitSPIRV(profile, program);
    CHECK(code == Backend::SPIRV::EmitSPIRV(profile, program));

    u32 immediate_id{};
    size_t phis{};
    for (size_t offset = 5; offset < code.size();) {
        const u32 words = code[offset] >> 16;
        REQUIRE(words != 0);
        REQUIRE(words <= code.size() - offset);
        const auto opcode = static_cast<spv::Op>(code[offset] & 0xffffU);
        if (opcode == spv::Op::OpConstant && words == 4 && code[offset + 3] == immediate) {
            immediate_id = code[offset + 2];
        }
        if (opcode == spv::Op::OpPhi) {
            REQUIRE(words == 7);
            REQUIRE(immediate_id != 0);
            CHECK(code[offset + 3] == immediate_id);
            CHECK(code[offset + 4] == entry.Definition<u32>());
            CHECK(code[offset + 5] == incremented.Inst()->Definition<u32>());
            CHECK(code[offset + 6] == taken.Definition<u32>());
            ++phis;
        }
        offset += words;
    }
    CHECK(phis == 1);
    if (const char* output = std::getenv("EDEN_SPIRV_PHI_TEST_OUTPUT")) {
        std::filesystem::create_directories(output);
        const auto path = std::filesystem::path{output} / (std::string{mode} + ".spv");
        std::ofstream file{path, std::ios::binary};
        file.write(reinterpret_cast<const char*>(code.data()),
                   static_cast<std::streamsize>(code.size() * sizeof(u32)));
        REQUIRE(file.good());
    }
}
