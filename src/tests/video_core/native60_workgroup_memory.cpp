// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"

namespace {
using namespace Shader;
constexpr u32 OP_COOPERATIVE_MATRIX_LOAD = 4457;
constexpr u32 OP_COOPERATIVE_MATRIX_STORE = 4458;
constexpr u32 CAPABILITY_VULKAN_MEMORY_MODEL = 5345;
constexpr u32 CAPABILITY_VULKAN_MEMORY_MODEL_DEVICE_SCOPE = 5346;
namespace Op { constexpr u32 Load = 61, Store = 62; }
struct TensorProgram {
    ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    Profile profile;

    explicit TensorProgram(u32 count = 1, u32 software_depth = 0) {
        program.stage = Stage::Compute;
        program.workgroup_size = {64, 1, 1};
        program.blocks = {&block};
        program.post_order_blocks = {&block};
        program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                               {{}, IR::AbstractSyntaxNode::Type::Return}};
        program.info.uses_subgroup_shuffles = count != 0 && software_depth != 0;
        program.info.uses_subgroup_invocation_id = count != 0;
        profile.supported_spirv = 0x00010500;
        profile.supported_subgroup_stages = 1U << static_cast<u32>(Stage::Compute);
        profile.max_subgroup_size = 32;
        IR::IREmitter ir{block};
        if (count == 0) {
            return;
        }
        const auto lane = ir.LaneId();
        std::array<IR::U32, 8> inputs;
        for (u32 i = 0; i < inputs.size(); ++i) {
            inputs[i] = software_depth ? ir.IAdd(ir.Imm32(0x3c003c00U + i * 0x01000100U),
                ir.ShiftLeftLogical(lane, ir.Imm32(3))) : ir.IAdd(ir.Imm32((i + 1) * 0x01000100U), lane);
        }
        for (u32 i = 0; i < count; ++i) {
            const auto result = ir.TensorMulAdd(
                                       ir.CompositeConstruct(inputs[0], inputs[1], inputs[2], inputs[3]),
                                       ir.CompositeConstruct(inputs[4], inputs[5]),
                                       ir.CompositeConstruct(inputs[6], inputs[7]), software_depth);
            inputs[6] = IR::U32{ir.CompositeExtract(result, 0)};
            inputs[7] = IR::U32{ir.CompositeExtract(result, 1)};
        }
        if (software_depth) {
            program.info.used_storage_buffer_types = IR::Type::U32;
            program.info.storage_buffers_descriptors = {{0, 0, 1, true}};
            program.info.uses_local_invocation_id = true;
            const auto offset = ir.ShiftLeftLogical(ir.LocalInvocationIdX(), ir.Imm32(3));
            block.AppendNewInst(IR::Opcode::WriteStorage32, {ir.Imm32(0), offset, inputs[6]});
            block.AppendNewInst(IR::Opcode::WriteStorage32,
                {ir.Imm32(0), ir.IAdd(offset, ir.Imm32(4)), inputs[7]});
        }
    }

    std::vector<u32> Emit(std::string_view name = {}) {
        auto code = Backend::SPIRV::EmitSPIRV(profile, program);
        if (const char* directory = std::getenv("EDEN_TENSOR_TEST_OUTPUT");
            directory && !name.empty()) {
            std::filesystem::create_directories(directory);
            std::ofstream file{std::filesystem::path{directory} / (std::string{name} + ".spv"),
                               std::ios::binary};
            file.write(reinterpret_cast<const char*>(code.data()),
                       static_cast<std::streamsize>(code.size() * sizeof(u32)));
            REQUIRE(file.good());
        }
        return code;
    }
};

std::vector<std::vector<u32>> Instructions(const std::vector<u32>& code, u32 opcode) {
    std::vector<std::vector<u32>> result;
    for (size_t offset = 5; offset < code.size();) {
        const u32 size = code[offset] >> 16;
        REQUIRE(size != 0);
        REQUIRE(offset + size <= code.size());
        if ((code[offset] & 0xffffU) == opcode) {
            result.emplace_back(code.begin() + offset + 1, code.begin() + offset + size);
        }
        offset += size;
    }
    return result;
}

bool HasCapability(const std::vector<u32>& code, u32 capability) {
    const auto capabilities = Instructions(code, 17);
    return std::ranges::any_of(capabilities, [=](const auto& words) {
        return words[0] == capability;
    });
}


std::map<u32, u32> Constants(const std::vector<u32>& code) {
    std::map<u32, u32> constants;
    for (const auto& words : Instructions(code, 43)) { // OpConstant
        constants.emplace(words[1], words[2]);
    }
    return constants;
}


void RequireVulkanMemoryModel(const std::vector<u32>& code) {
    REQUIRE(HasCapability(code, CAPABILITY_VULKAN_MEMORY_MODEL));
    REQUIRE(HasCapability(code, CAPABILITY_VULKAN_MEMORY_MODEL_DEVICE_SCOPE));
    const auto models = Instructions(code, 14); // OpMemoryModel
    REQUIRE(models.size() == 1);
    REQUIRE(models[0][1] == 3); // Vulkan
    bool extension = false;
    for (const auto& words : Instructions(code, 10)) { // OpExtension
        extension |= std::string{reinterpret_cast<const char*>(words.data())} ==
                     "SPV_KHR_vulkan_memory_model";
    }
    REQUIRE(extension == (code[1] < 0x00010500));
}

struct WorkgroupMemoryOperands {
    u32 loads{};
    u32 stores{};
    u32 cooperative_loads{};
    u32 cooperative_stores{};
    u32 barriers{};
};

// Under the Vulkan memory model a plain access is private and can't carry data between
// invocations, so every workgroup memory access must be non-private and make itself available or
// visible to the workgroup, and every barrier must make memory available and visible. A GLSL450
// module carries none of these operands.
WorkgroupMemoryOperands RequireWorkgroupMemoryOperands(const std::vector<u32>& code,
                                                       bool vulkan_memory_model) {
    constexpr u32 STORAGE_CLASS_WORKGROUP = 4;
    constexpr u32 SCOPE_WORKGROUP = 2;
    constexpr u32 MAKE_POINTER_AVAILABLE = 0x8, MAKE_POINTER_VISIBLE = 0x10,
                  NON_PRIVATE_POINTER = 0x20;
    constexpr u32 MAKE_AVAILABLE = 0x2000, MAKE_VISIBLE = 0x4000;
    const auto constants = Constants(code);
    std::set<u32> pointer_types;
    for (const auto& words : Instructions(code, 32)) { // OpTypePointer
        if (words[1] == STORAGE_CLASS_WORKGROUP) {
            pointer_types.insert(words[0]);
        }
    }
    std::set<u32> pointers;
    for (const u32 opcode : {59U, 65U}) { // OpVariable, OpAccessChain
        for (const auto& words : Instructions(code, opcode)) {
            if (pointer_types.contains(words[0])) {
                pointers.insert(words[1]);
            }
        }
    }
    WorkgroupMemoryOperands result;
    struct Access {
        u32 opcode;
        size_t pointer;
        size_t operands;
        u32 availability;
        u32 WorkgroupMemoryOperands::*count;
    };
    for (const Access& access : {
             Access{Op::Load, 2, 3, MAKE_POINTER_VISIBLE, &WorkgroupMemoryOperands::loads},
             Access{Op::Store, 0, 2, MAKE_POINTER_AVAILABLE, &WorkgroupMemoryOperands::stores},
             Access{OP_COOPERATIVE_MATRIX_LOAD, 2, 5, MAKE_POINTER_VISIBLE,
                    &WorkgroupMemoryOperands::cooperative_loads},
             Access{OP_COOPERATIVE_MATRIX_STORE, 0, 4, MAKE_POINTER_AVAILABLE,
                    &WorkgroupMemoryOperands::cooperative_stores}}) {
        for (const auto& words : Instructions(code, access.opcode)) {
            if (!pointers.contains(words[access.pointer])) {
                continue;
            }
            ++(result.*access.count);
            if (!vulkan_memory_model) {
                REQUIRE(words.size() == access.operands);
                continue;
            }
            REQUIRE(words.size() == access.operands + 2);
            REQUIRE(words[access.operands] == (access.availability | NON_PRIVATE_POINTER));
            REQUIRE(constants.at(words[access.operands + 1]) == SCOPE_WORKGROUP);
        }
    }
    const u32 expected_semantics = vulkan_memory_model ? MAKE_AVAILABLE | MAKE_VISIBLE : 0;
    for (const auto& [opcode, semantics] : {std::pair{224U, 2U}, std::pair{225U, 1U}}) {
        for (const auto& words : Instructions(code, opcode)) { // OpControlBarrier, OpMemoryBarrier
            REQUIRE((constants.at(words[semantics]) & (MAKE_AVAILABLE | MAKE_VISIBLE)) ==
                    expected_semantics);
            ++result.barriers;
        }
    }
    return result;
}

} // namespace

TEST_CASE("Native60: shared access widths and atomic fallbacks retain workgroup operands",
          "[video_core][tensor][native60]") {
    for (const bool explicit_layout : {false, true}) {
        CAPTURE(explicit_layout);
        TensorProgram fixture{1, 16};
        fixture.profile.supported_spirv = 0x00010600;
        fixture.profile.support_cooperative_hmma = true;
        fixture.profile.support_explicit_workgroup_layout = explicit_layout;
        fixture.profile.support_workgroup_layout_8bit_access = true;
        fixture.profile.support_workgroup_layout_16bit_access = true;
        fixture.profile.support_int8 = true;
        fixture.profile.support_int16 = true;
        fixture.profile.support_int64 = true;
        fixture.program.shared_memory_size = 256;
        fixture.program.info.uses_int8 = true;
        fixture.program.info.uses_int16 = true;
        fixture.program.info.uses_int64 = true;
        fixture.program.info.uses_shared_increment = true;
        fixture.program.info.uses_shared_decrement = true;
        IR::IREmitter ir{fixture.block};
        for (const int width : {8, 16, 32, 64, 128}) {
            const auto value = ir.LoadShared(width, false, ir.Imm32(32));
            ir.WriteShared(width, ir.Imm32(64), value);
        }
        static_cast<void>(ir.LoadShared(8, true, ir.Imm32(32)));
        static_cast<void>(ir.LoadShared(16, true, ir.Imm32(32)));
        static_cast<void>(ir.SharedAtomicInc(ir.Imm32(96), ir.Imm32(7)));
        static_cast<void>(ir.SharedAtomicDec(ir.Imm32(100), ir.Imm32(7)));
        static_cast<void>(ir.SharedAtomicExchange(ir.Imm32(112), ir.Imm64(0x123456789abcdef0ULL)));
        ir.Barrier();
        const auto code = fixture.Emit(explicit_layout ? "workgroup-explicit-widths"
                                                        : "workgroup-word-widths");
        RequireVulkanMemoryModel(code);
        const auto operands = RequireWorkgroupMemoryOperands(code, true);
        CHECK(operands.loads >= 7);
        CHECK(operands.stores >= 5);
        CHECK(operands.barriers == 1);
    }
}

TEST_CASE("Native60: cooperative HMMA keeps workgroup memory coherent under the Vulkan memory model",
          "[video_core][tensor][ampere]") {
    const auto emit = [](std::string_view variant, auto configure) {
        TensorProgram fixture{1, 16};
        fixture.profile.supported_spirv = 0x00010600;
        fixture.program.shared_memory_size = 64;
        configure(fixture.profile);
        IR::IREmitter ir{fixture.block};
        ir.WriteShared(32, ir.Imm32(4), ir.LaneId());
        ir.Barrier();
        static_cast<void>(ir.LoadShared(32, false, ir.Imm32(8)));
        ir.WorkgroupMemoryBarrier();
        ir.DeviceMemoryBarrier();
        return fixture.Emit("workgroup-memory-" + std::string{variant});
    };
    SECTION("16x8x16") {
        const auto code = emit("16x8x16", [](Profile& profile) {
            profile.support_cooperative_hmma = true;
        });
        RequireVulkanMemoryModel(code);
        const auto operands = RequireWorkgroupMemoryOperands(code, true);
        REQUIRE(operands.loads == 1);
        REQUIRE(operands.stores == 1);
        REQUIRE(operands.barriers == 3);
    }
    SECTION("16x16x16") {
        const auto code = emit("16x16x16", [](Profile& profile) {
            profile.support_cooperative_hmma_16x16x16 = true;
        });
        RequireVulkanMemoryModel(code);
        const auto operands = RequireWorkgroupMemoryOperands(code, true);
        REQUIRE(operands.loads == 3);   // The guest's load and the lane's two D words.
        REQUIRE(operands.stores == 11); // The guest's store and the lane's A, B and C words.
        REQUIRE(operands.cooperative_loads == 3);
        REQUIRE(operands.cooperative_stores == 1);
        REQUIRE(operands.barriers == 5);
    }
    SECTION("8x16x16") {
        const auto code = emit("8x16x16", [](Profile& profile) {
            profile.support_cooperative_hmma_8x16x16 = true;
        });
        RequireVulkanMemoryModel(code);
        const auto operands = RequireWorkgroupMemoryOperands(code, true);
        REQUIRE(operands.loads == 3);
        REQUIRE(operands.stores == 11);
        REQUIRE(operands.cooperative_loads == 5);
        REQUIRE(operands.cooperative_stores == 2);
        REQUIRE(operands.barriers == 5);
    }
    SECTION("software HMMA on GLSL450") {
        const auto code = emit("software", [](Profile&) {});
        REQUIRE_FALSE(HasCapability(code, CAPABILITY_VULKAN_MEMORY_MODEL));
        const auto operands = RequireWorkgroupMemoryOperands(code, false);
        REQUIRE(operands.loads == 1);
        REQUIRE(operands.stores == 1);
        REQUIRE(operands.barriers == 3);
    }
}
