// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/ir_opt/passes.h"

namespace {
using namespace Shader;
using Ampere::Instruction;
using Ampere::LowerToMaxwell;

constexpr u32 Data = 28;
constexpr u32 Coordinate = 65;
constexpr u32 Handle = 47;
constexpr u32 CbufOffset = 0x98;
constexpr Instruction Exit{{0x794d, 0, 0x03800000, 0}};

constexpr Instruction Formatted(bool store, u32 dimension, u32 channels, u32 data = Data,
                                u32 coordinate = Coordinate, u32 handle = Handle) {
    return {{(store ? 0x799cU : 0x7998U) | (coordinate << 24) | (store ? 0 : data << 16),
             (dimension << 29) | (store ? data : 0),
             (store ? 0x00100000U : 0x001e0000U) | (channels << 8) | handle, 0}};
}

constexpr Instruction Atomic(bool reduction, u32 dimension, u32 operation, u32 dest = Data,
                             u32 coordinate = Coordinate, u32 data = Data + 4,
                             u32 handle = Handle) {
    return {{(reduction ? 0x73a0U : 0x7394U) | (coordinate << 24) |
                 (reduction ? 0 : dest << 16),
             (dimension << 29) | data,
             (reduction ? 0x0010e000U : 0x001ee000U) | (operation << 23) | handle, 0}};
}

u32 Field(u64 word, u32 shift, u64 mask = 0xff) {
    return static_cast<u32>((word >> shift) & mask);
}

TEST_CASE("Synthetic Ampere bindless formatted surfaces preserve operands",
          "[video_core][ampere][bindless_surfaces]") {
    struct Case { bool store; u32 dimension; u32 channels; };
    for (const auto [store, dimension, channels] : std::array{
             Case{false, 3, 15}, Case{true, 3, 1}, Case{false, 5, 7},
             Case{true, 5, 3}, Case{true, 4, 15}, Case{false, 1, 5}, Case{true, 1, 15},
         }) {
        CAPTURE(store, dimension, channels);
        auto instruction = Formatted(store, dimension, channels);
        for (const u32 predicate : {7U, 9U}) {
            instruction.words[0] = (instruction.words[0] & ~0xf000U) | (predicate << 12);
            const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
            INFO(lowered.error);
            REQUIRE(lowered);
            REQUIRE(lowered.instructions.size() == 2);
            CHECK(lowered.required_texture_buffer == ~0U);
            const u64 word{lowered.instructions.front()};
            CHECK(Field(word, 56) == 0xeb);
            CHECK(Field(word, 52, 3) == (store ? 2U : 0U));
            CHECK(Field(word, 51, 1) == 0);
            CHECK(Field(word, 20, 15) == channels);
            CHECK(Field(word, 33, 7) == dimension);
            CHECK(Field(word, 16, 15) == predicate);
            CHECK(Field(word, 39) == Handle);
            CHECK(Field(word, 8) == Coordinate);
            CHECK(Field(word, 0) == Data);
        }
    }
}

TEST_CASE("Synthetic Ampere formatted surface guards preserve scratch registers",
          "[video_core][ampere][bindless_surfaces]") {
    for (const bool store : {false, true}) {
        const auto original = Formatted(store, 3, 15);
        for (const auto& [word, bit] : std::array{
                 std::pair{1U, 8U}, std::pair{1U, 28U}, std::pair{2U, 12U},
                 std::pair{2U, 13U}, std::pair{2U, 20U}, std::pair{2U, 31U},
                 std::pair{3U, 0U}, std::pair{3U, 30U}}) {
            auto invalid = original;
            invalid.words[word] ^= 1U << bit;
            CHECK_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
        }
        CHECK_FALSE(LowerToMaxwell(std::array{Formatted(store, 3, 0), Exit}));
        CHECK_FALSE(LowerToMaxwell(std::array{Formatted(store, 0, 15), Exit}));
        CHECK_FALSE(LowerToMaxwell(std::array{Formatted(store, 2, 15), Exit}));
        CHECK_FALSE(LowerToMaxwell(std::array{Formatted(store, 3, 15, Data, Coordinate, 250), Exit}));
        CHECK_FALSE(LowerToMaxwell(std::array{Formatted(store, 5, 15, Data, 244), Exit}));
        CHECK_FALSE(LowerToMaxwell(std::array{Formatted(store, 3, 15, 244), Exit}));
        CHECK(LowerToMaxwell(std::array{Formatted(store, 1, 1, 245, 245, 245), Exit}));
    }
    auto unexpected_load_source = Formatted(false, 3, 15);
    unexpected_load_source.words[1] |= 1;
    CHECK_FALSE(LowerToMaxwell(std::array{unexpected_load_source, Exit}));
    CHECK_FALSE(LowerToMaxwell(std::array{Formatted(false, 3, 15, Data + 1), Exit}));
    auto unexpected_store_destination = Formatted(true, 3, 15);
    unexpected_store_destination.words[0] |= 1U << 16;
    CHECK_FALSE(LowerToMaxwell(std::array{unexpected_store_destination, Exit}));
    CHECK_FALSE(LowerToMaxwell(std::array{Formatted(true, 3, 5), Exit}));

    const Instruction uniform{{0x00047882, 0, 0, 0}};
    REQUIRE(LowerToMaxwell(std::array{uniform, Exit}));
    CHECK_FALSE(LowerToMaxwell(std::array{uniform, Formatted(true, 3, 15, 214), Exit}));
}

TEST_CASE("Synthetic Ampere register atomics preserve the handle and predicate ABI",
          "[video_core][ampere][bindless_surfaces]") {
    for (const bool reduction : {false, true}) {
        for (const u32 dimension : {1U, 3U, 5U}) {
            const u32 operation = reduction ? 7U : 8U;
            auto instruction = Atomic(reduction, dimension, operation);
            instruction.words[0] = (instruction.words[0] & ~0xf000U) | 0xa000U;
            const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
            INFO(lowered.error);
            REQUIRE(lowered);
            REQUIRE(lowered.instructions.size() == 2);
            CHECK(lowered.required_texture_buffer == ~0U);
            const u64 word{lowered.instructions.front()};
            CHECK(Field(word, 56) == (reduction ? 0xebU : 0xeaU));
            CHECK(Field(word, 16, 15) == 10);
            CHECK(Field(word, 33, 7) == dimension);
            CHECK(Field(word, 39) == Handle);
            CHECK(Field(word, 8) == Coordinate);
            if (reduction) {
                CHECK(Field(word, 51, 1) == 0);
                CHECK(Field(word, 24, 7) == operation);
                CHECK(Field(word, 0) == Data + 4);
            } else {
                CHECK(Field(word, 54, 1) == 1);
                CHECK(Field(word, 29, 15) == operation);
                CHECK(Field(word, 20) == Data + 4);
                CHECK(Field(word, 0) == Data);
            }
        }
    }
    // A bound atomic still chooses the buffer for the stage, independently of the register handle.
    auto bound = Atomic(false, 1, 0, Data, Coordinate, Data + 4, 0);
    bound.words[0] = (bound.words[0] & ~0xfffU) | 0x593U;
    bound.words[1] |= (4U << 22) | (9U << 8);
    const auto mixed = LowerToMaxwell(std::array{bound, Atomic(false, 1, 0), Exit});
    REQUIRE(mixed);
    CHECK(mixed.required_texture_buffer == 4);
    CHECK(Field(mixed.instructions.front(), 54, 1) == 0);
    CHECK(Field(mixed.instructions.front(), 36, 0x1fff) == 9);
}

TEST_CASE("Synthetic Ampere register atomics reject unsupported or reserved operands",
          "[video_core][ampere][bindless_surfaces]") {
    for (const bool reduction : {false, true}) {
        for (const u32 dimension : {2U, 6U, 7U}) {
            CHECK_FALSE(LowerToMaxwell(std::array{Atomic(reduction, dimension, 0), Exit}));
        }
        for (const u32 operation : {3U, 4U, 9U}) {
            CHECK_FALSE(LowerToMaxwell(std::array{Atomic(reduction, 1, operation), Exit}));
        }
        CHECK_FALSE(LowerToMaxwell(std::array{Atomic(reduction, 5, 0, Data, 244), Exit}));
        CHECK_FALSE(LowerToMaxwell(std::array{Atomic(reduction, 1, 0, Data, Coordinate, 247), Exit}));
        CHECK_FALSE(LowerToMaxwell(std::array{
            Atomic(reduction, 1, 0, Data, Coordinate, Data + 4, 255), Exit}));
        CHECK(LowerToMaxwell(std::array{Atomic(reduction, 1, 0, Data, Coordinate, 255), Exit}));
        auto combined_descriptors = Atomic(reduction, 1, 0);
        combined_descriptors.words[1] |= 1U << 8;
        CHECK_FALSE(LowerToMaxwell(std::array{combined_descriptors, Exit}));
        auto unknown_modifier = Atomic(reduction, 1, 0);
        unknown_modifier.words[2] ^= 1U << 16;
        CHECK_FALSE(LowerToMaxwell(std::array{unknown_modifier, Exit}));
        auto unknown_tail = Atomic(reduction, 1, 0);
        unknown_tail.words[3] = 1;
        CHECK_FALSE(LowerToMaxwell(std::array{unknown_tail, Exit}));
    }
    const auto unused_destination = Atomic(false, 1, 0, 255);
    CHECK(LowerToMaxwell(std::array{unused_destination, Exit}));
    auto reserved_reduction_destination = Atomic(true, 1, 0);
    reserved_reduction_destination.words[0] |= 1U << 16;
    CHECK_FALSE(LowerToMaxwell(std::array{reserved_reduction_destination, Exit}));
}

TEST_CASE("Ampere surface loads cannot overwrite a return link with any output word",
          "[video_core][ampere][bindless_surfaces]") {
    constexpr u32 link = Data + 1;
    constexpr Instruction link_mov{{0x7802U | (link << 16), 0x20, 0xf00, 0}};
    constexpr Instruction call{{0x7944, 0x10, 0x03c00000, 0}};
    constexpr Instruction ret{{0x7950U | (link << 24), 0xffffffb0, 0x03c3ffff, 0}};
    const auto clear = Formatted(false, 1, 15, Data + 4);
    const auto clobber = Formatted(false, 1, 15);
    const auto safe = LowerToMaxwell(std::array{link_mov, call, Exit, clear, ret});
    INFO(safe.error);
    REQUIRE(safe);
    CHECK_FALSE(LowerToMaxwell(std::array{link_mov, call, Exit, clobber, ret}));
}

class SurfaceEnvironment final : public Environment {
public:
    explicit SurfaceEnvironment(TextureType type_) : type{type_} { stage = Stage::Compute; }
    u64 ReadInstruction(u32) override { throw LogicError("Unexpected instruction read"); }
    u32 ReadCbufValue(u32 bank, u32 offset) override {
        CHECK(bank == 2);
        CHECK(offset == CbufOffset);
        return 19;
    }
    TextureType ReadTextureType(u32) override { return type; }
    TexturePixelFormat ReadTexturePixelFormat(u32) override { return TexturePixelFormat::R32_UINT; }
    bool IsTexturePixelFormatInteger(u32) override { return true; }
    u32 ReadViewportTransformState() override { return 1; }
    u32 TextureBoundBuffer() const override { return 0; }
    u32 LocalMemorySize() const override { return 0; }
    u32 SharedMemorySize() const override { return 0; }
    std::array<u32, 3> WorkgroupSize() const override { return {1, 1, 1}; }
    bool HasHLEMacroState() const override { return false; }
    bool HasNativeInstructions() const override { return true; }
    std::optional<ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}
private:
    TextureType type;
};

TEST_CASE("Lowered register surfaces resolve to the correct Vulkan descriptor and operation",
          "[video_core][ampere][bindless_surfaces]") {
    enum class Operation { Read, Store, Atomic, Reduction };
    struct Case {
        Operation operation;
        u32 dimension;
        TextureType type;
        u32 spirv_opcode;
        ImageFormat format;
    };
    for (const auto [operation, dimension, type, spirv_opcode, format] : std::array{
             Case{Operation::Read, 1, TextureType::Buffer, 98, ImageFormat::Typeless},
             Case{Operation::Store, 1, TextureType::Buffer, 99, ImageFormat::Typeless},
             Case{Operation::Atomic, 1, TextureType::Buffer, 234, ImageFormat::R32_UINT},
             Case{Operation::Atomic, 3, TextureType::Color2D, 234, ImageFormat::R32_UINT},
             Case{Operation::Reduction, 5, TextureType::Color3D, 242, ImageFormat::R32_UINT},
         }) {
        CAPTURE(operation, dimension);
        const bool writes = operation != Operation::Read;
        const bool reads = operation != Operation::Store;
        const bool atomic = operation == Operation::Atomic || operation == Operation::Reduction;
        const auto source = atomic ?
            Atomic(operation == Operation::Reduction, dimension,
                   operation == Operation::Reduction ? 7 : 0) :
            Formatted(writes, dimension, 1);
        const auto lowered = LowerToMaxwell(std::array{source, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        SurfaceEnvironment env{type};
        ObjectPool<IR::Inst> pool{256};
        IR::Block block{pool};
        Maxwell::TranslatorVisitor visitor{env, block};
        auto& ir = visitor.ir;
        visitor.X(static_cast<IR::Reg>(Handle), ir.GetCbuf(ir.Imm32(2), ir.Imm32(CbufOffset)));
        for (u32 i = 0; i < 3; ++i) {
            visitor.X(static_cast<IR::Reg>(Coordinate + i), ir.Imm32(11 + i));
        }
        visitor.X(static_cast<IR::Reg>(Data), ir.Imm32(41));
        visitor.X(static_cast<IR::Reg>(Data + 4), ir.Imm32(5));
        const auto binding_writes = [&] {
            return std::ranges::count_if(block.Instructions(), [](const IR::Inst& inst) {
                return inst.GetOpcode() == IR::Opcode::SetRegister &&
                       inst.Arg(0).Reg() == static_cast<IR::Reg>(Data);
            });
        };
        const auto before = binding_writes();
        const u64 word = lowered.instructions.front();
        switch (operation) {
        case Operation::Read: visitor.SULD(word); break;
        case Operation::Store: visitor.SUST(word); break;
        case Operation::Atomic: visitor.SUATOM(word); break;
        case Operation::Reduction: visitor.SURED(word); break;
        }
        CHECK(binding_writes() == before + (operation == Operation::Read ||
                                           operation == Operation::Atomic ? 1 : 0));
        IR::Program program;
        if (operation == Operation::Read) {
            const auto value = visitor.X(static_cast<IR::Reg>(Data));
            block.AppendNewInst(IR::Opcode::WriteStorage32, {ir.Imm32(0), ir.Imm32(0), value});
            program.info.storage_buffers_descriptors.push_back(
                {.cbuf_index = 3, .cbuf_offset = 0x150, .count = 1, .is_written = true});
        }
        program.stage = Stage::Compute;
        program.workgroup_size = {1, 1, 1};
        program.blocks = {&block};
        program.post_order_blocks = {&block};
        program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                               {{}, IR::AbstractSyntaxNode::Type::Return}};
        Optimization::SsaRewritePass(program);
        HostTranslateInfo host{};
        host.ApplyDescriptorLimitPolicy();
        Optimization::TexturePass(env, program, host);
        Optimization::DeadCodeEliminationPass(program);
        Optimization::CollectShaderInfoPass(env, program);
        const auto check = [&](const auto& descriptor) {
            CHECK(descriptor.cbuf_index == 2);
            CHECK(descriptor.cbuf_offset == CbufOffset);
            CHECK(descriptor.count == 1);
            CHECK(descriptor.is_written == writes);
            CHECK(descriptor.is_read == reads);
            CHECK(descriptor.format == format);
        };
        if (type == TextureType::Buffer) {
            REQUIRE(program.info.image_buffer_descriptors.size() == 1);
            CHECK(program.info.image_descriptors.empty());
            check(program.info.image_buffer_descriptors.front());
        } else {
            REQUIRE(program.info.image_descriptors.size() == 1);
            CHECK(program.info.image_buffer_descriptors.empty());
            const auto& descriptor = program.info.image_descriptors.front();
            CHECK(descriptor.type == type);
            check(descriptor);
        }
        Profile profile{};
        profile.supported_spirv = 0x00010300;
        profile.support_typeless_image_loads = true;
        profile.unified_descriptor_binding = true;
        const auto code = Backend::SPIRV::EmitSPIRV(profile, program);
        if (const char* directory = std::getenv("EDEN_BINDLESS_SURFACE_SPIRV_OUTPUT")) {
            const auto path = std::filesystem::path{directory} /
                              ("surface-" + std::to_string(spirv_opcode) + "-" +
                               std::to_string(dimension) + ".spv");
            std::ofstream binary{path, std::ios::binary};
            binary.write(reinterpret_cast<const char*>(code.data()),
                         static_cast<std::streamsize>(code.size() * sizeof(u32)));
            REQUIRE(binary.good());
        }
        constexpr u32 OpCapability = 17;
        constexpr u32 ImageBufferCapability = 47;
        bool image_buffer_enabled = false;
        size_t operations = 0;
        for (size_t i = 5; i < code.size();) {
            const u32 count = code[i] >> 16;
            REQUIRE(count > 0);
            REQUIRE(i + count <= code.size());
            const u32 opcode = code[i] & 0xffff;
            image_buffer_enabled |= opcode == OpCapability && code[i + 1] == ImageBufferCapability;
            operations += opcode == spirv_opcode;
            i += count;
        }
        CHECK(image_buffer_enabled == (type == TextureType::Buffer));
        CHECK(operations == 1);
    }
}
} // namespace
