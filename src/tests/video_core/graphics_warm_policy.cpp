// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unordered_set>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/object_pool.h"
#include "video_core/renderer_vulkan/graphics_warm_policy.h"

namespace {

// Inspect the emitted interface, rather than assuming that the IR attribute is an input.
bool HasPositionInput(const std::vector<u32>& code) {
    constexpr u32 OpVariable = 59;
    constexpr u32 OpDecorate = 71;
    constexpr u32 DecorationBuiltIn = 11;
    constexpr u32 BuiltInPosition = 0;
    constexpr u32 StorageClassInput = 1;
    std::unordered_set<u32> position_ids;
    std::unordered_set<u32> input_ids;
    for (size_t offset = 5; offset < code.size();) {
        const u32 words = code[offset] >> 16;
        REQUIRE(words != 0);
        REQUIRE(offset + words <= code.size());
        const u32 op = code[offset] & 0xffff;
        if (op == OpDecorate && words >= 4 && code[offset + 2] == DecorationBuiltIn &&
            code[offset + 3] == BuiltInPosition) {
            position_ids.insert(code[offset + 1]);
        } else if (op == OpVariable && words >= 4 && code[offset + 3] == StorageClassInput) {
            input_ids.insert(code[offset + 2]);
        }
        offset += words;
    }
    for (const u32 id : position_ids) {
        if (input_ids.contains(id)) {
            return true;
        }
    }
    return false;
}

std::vector<u32> Emit(Shader::IR::Program& program) {
    Shader::ObjectPool<Shader::IR::Inst> inst_pool;
    Shader::IR::Block block{inst_pool};
    program.blocks.push_back(&block);
    program.post_order_blocks.push_back(&block);
    Shader::IR::AbstractSyntaxNode block_node{};
    block_node.type = Shader::IR::AbstractSyntaxNode::Type::Block;
    block_node.data.block = &block;
    Shader::IR::AbstractSyntaxNode return_node{};
    return_node.type = Shader::IR::AbstractSyntaxNode::Type::Return;
    program.syntax_list = {block_node, return_node};
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010300;
    return Shader::Backend::SPIRV::EmitSPIRV(profile, program);
}

} // namespace

TEST_CASE("Graphics warm-up rejects vertex position inputs before driver compilation",
          "[video_core][gpl][spirv]") {
    using Shader::IR::Attribute;
    for (const auto component :
         {Attribute::PositionX, Attribute::PositionY, Attribute::PositionZ, Attribute::PositionW}) {
        Shader::IR::Program program;
        program.stage = Shader::Stage::VertexB;
        program.info.loads.Set(component);
        CHECK_FALSE(Vulkan::CanWarmGraphicsProgram(program));
        CHECK(HasPositionInput(Emit(program)));
    }
}

TEST_CASE("Graphics warm-up keeps ordinary vertex inputs and fragment position reads",
          "[video_core][gpl][spirv]") {
    Shader::IR::Program vertex;
    vertex.stage = Shader::Stage::VertexB;
    vertex.info.loads.Set(Shader::IR::Attribute::Generic0X);
    CHECK(Vulkan::CanWarmGraphicsProgram(vertex));
    CHECK_FALSE(HasPositionInput(Emit(vertex)));

    Shader::IR::Program fragment;
    fragment.stage = Shader::Stage::Fragment;
    fragment.info.loads.Set(Shader::IR::Attribute::PositionX);
    CHECK(Vulkan::CanWarmGraphicsProgram(fragment));
    CHECK_FALSE(HasPositionInput(Emit(fragment)));
}
