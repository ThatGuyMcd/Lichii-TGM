// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/object_pool.h"
#include "video_core/renderer_vulkan/noperspective_policy.h"

namespace {
namespace IR = Shader::IR;

struct Fixture {
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    Shader::RuntimeInfo runtime;
    Shader::Profile profile{};

    explicit Fixture(Shader::Stage stage) {
        program.stage = stage;
        program.blocks = {&block};
        program.post_order_blocks = {&block};
        program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                               {{}, IR::AbstractSyntaxNode::Type::Return}};
        profile.supported_spirv = 0x00010300;
        profile.unified_descriptor_binding = true;
        runtime.previous_stage_stores.mask.set();
        IR::IREmitter ir{block};
        ir.Prologue();
        if (stage == Shader::Stage::VertexB) {
            for (u32 component = 0; component < 4; ++component) {
                const auto attribute = IR::Attribute::PositionX + component;
                program.info.stores.Set(attribute);
                ir.SetAttribute(attribute, ir.Imm32(component == 3 ? 2.0f : 0.5f),
                                ir.Imm32(0));
            }
            program.info.stores.Set(IR::Attribute::Generic0X);
            ir.SetAttribute(IR::Attribute::Generic0X, ir.Imm32(0.25f), ir.Imm32(0));
            runtime.noperspective_outputs.set(0);
        } else {
            program.info.loads.Set(IR::Attribute::Generic0X);
            program.info.interpolation[0] = Shader::Interpolation::NoPerspective;
            program.info.stores_frag_color[0] = true;
            program.info.frag_color_components[0] = 1;
            ir.SetFragColor(0, 0, ir.GetAttribute(IR::Attribute::Generic0X));
            runtime.scaled_noperspective_inputs = true;
        }
        ir.Epilogue();
    }

    std::vector<u32> Emit(u32 vendor_id) {
        Vulkan::ApplyNoPerspectiveInterpolationPolicy(runtime, vendor_id);
        Shader::Backend::Bindings bindings;
        return Shader::Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
    }
};

size_t CountOpcode(const std::vector<u32>& code, u32 wanted) {
    size_t count{};
    for (size_t offset = 5; offset < code.size();) {
        const u32 words = code[offset] >> 16;
        REQUIRE(words > 0);
        REQUIRE(words <= code.size() - offset);
        count += (code[offset] & 0xffff) == wanted;
        offset += words;
    }
    return count;
}

bool HasDecoration(const std::vector<u32>& code, u32 decoration, u32 operand = ~0U) {
    for (size_t offset = 5; offset < code.size();) {
        const u32 words = code[offset] >> 16;
        REQUIRE(words > 0);
        REQUIRE(words <= code.size() - offset);
        if ((code[offset] & 0xffff) == 71 && words >= 3 &&
            code[offset + 2] == decoration &&
            (operand == ~0U || (words >= 4 && code[offset + 3] == operand))) {
            return true;
        }
        offset += words;
    }
    return false;
}
} // Anonymous namespace

TEST_CASE("NVIDIA keeps native noperspective interpolation in both shader stages",
          "[video_core][spirv][noperspective]") {
    Fixture vertex{Shader::Stage::VertexB};
    Fixture fragment{Shader::Stage::Fragment};
    const auto vertex_code = vertex.Emit(0x10DE);
    const auto fragment_code = fragment.Emit(0x10DE);
    CHECK(vertex.runtime.noperspective_outputs.none());
    CHECK_FALSE(fragment.runtime.scaled_noperspective_inputs);
    CHECK(CountOpcode(vertex_code, 133) + CountOpcode(vertex_code, 142) == 0);
    CHECK(CountOpcode(fragment_code, 133) == 0); // no FragCoord.w compensation
    CHECK(HasDecoration(fragment_code, 13)); // NoPerspective
    CHECK_FALSE(HasDecoration(fragment_code, 11, 15)); // BuiltIn FragCoord
}

TEST_CASE("AMD retains paired noperspective clipping compensation",
          "[video_core][spirv][noperspective]") {
    Fixture vertex{Shader::Stage::VertexB};
    Fixture fragment{Shader::Stage::Fragment};
    const auto vertex_code = vertex.Emit(0x1002);
    const auto fragment_code = fragment.Emit(0x1002);
    CHECK(vertex.runtime.noperspective_outputs.test(0));
    CHECK(fragment.runtime.scaled_noperspective_inputs);
    // Generic outputs may be grouped into a vector: OpFMul or OpVectorTimesScalar.
    CHECK(CountOpcode(vertex_code, 133) + CountOpcode(vertex_code, 142) == 1);
    CHECK(CountOpcode(fragment_code, 133) == 1);
    CHECK_FALSE(HasDecoration(fragment_code, 13));
    CHECK(HasDecoration(fragment_code, 11, 15));
}

TEST_CASE("Noperspective policy preserves existing non-NVIDIA stage eligibility",
          "[video_core][spirv][noperspective]") {
    for (const u32 vendor : {0x1002U, 0x8086U, 0x5143U, 0U}) {
        CAPTURE(vendor);
        Shader::RuntimeInfo eligible;
        eligible.noperspective_outputs.set(0);
        eligible.noperspective_outputs.set(31);
        eligible.scaled_noperspective_inputs = true;
        Vulkan::ApplyNoPerspectiveInterpolationPolicy(eligible, vendor);
        CHECK(eligible.noperspective_outputs.count() == 2);
        CHECK(eligible.scaled_noperspective_inputs);
        Shader::RuntimeInfo ineligible;
        Vulkan::ApplyNoPerspectiveInterpolationPolicy(ineligible, vendor);
        CHECK(ineligible.noperspective_outputs.none());
        CHECK_FALSE(ineligible.scaled_noperspective_inputs);
    }
}
