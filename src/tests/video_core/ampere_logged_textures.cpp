// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"
#include "shader_recompiler/frontend/maxwell/structured_control_flow.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/ir_opt/passes.h"

namespace {
namespace IR = Shader::IR;
using Shader::Ampere::Instruction;
constexpr Instruction Exit{{0x794d, 0, 0x03800000, 0}};
constexpr u32 TextureBank = 6;
constexpr u32 TextureWord = 0x5d;
constexpr u32 NoRegister = 255;
constexpr u32 HalfOutput = 1U << 15;
constexpr u32 PerTap = 1U << 13;
constexpr u32 DepthCompare = 1U << 14;
constexpr u32 NoDependency = 1U << 26;

// These tests assemble individual texture encodings from independent operands.
struct TextureEncoding {
    u32 opcode;
    u32 destination;
    u32 coordinates;
    u32 operand;
    u32 second_destination = NoRegister;
    u32 mask = 15;
    u32 bank = TextureBank;
    u32 table_word = TextureWord;
    u32 dimension = 1;
    u32 guard = 7;
    u32 flags{};

    constexpr Instruction Assemble() const {
        return {{opcode | (guard << 12) | (destination << 16) | (coordinates << 24),
                 operand | (table_word << 8) | (bank << 22) | (1U << 28) | (dimension << 29),
                 second_destination | (mask << 8) | (7U << 17) | flags,
                 0}};
    }
};

constexpr TextureEncoding DepthSample{
    .opcode = 0xb60, .destination = 37, .coordinates = 20, .operand = 34,
    .mask = 1, .dimension = 5, .flags = DepthCompare | NoDependency};

constexpr u32 FetchDestination = 3;
constexpr u32 FetchSecondDestination = 9;
constexpr u32 FetchCoordinates = 22;
// The LOD lives in the first destination before the fetch overwrites it.
constexpr auto Fetch = TextureEncoding{
    .opcode = 0xb66, .destination = FetchDestination, .coordinates = FetchCoordinates,
    .operand = FetchDestination, .second_destination = FetchSecondDestination, .mask = 7,
    .flags = HalfOutput | (3U << 23) | NoDependency}.Assemble();

constexpr u32 GatherDestination = 14;
constexpr u32 GatherCoordinates = 32;
constexpr TextureEncoding HalfGather{
    .opcode = 0xb63, .destination = GatherDestination, .coordinates = GatherCoordinates,
    .operand = GatherCoordinates + 1, .second_destination = GatherDestination + 1,
    .flags = HalfOutput | NoDependency};
constexpr auto Gathers = std::array{
    HalfGather.Assemble(),
    TextureEncoding{
        .opcode = 0xb63, .destination = GatherDestination, .coordinates = GatherCoordinates,
        .operand = GatherCoordinates + 1, .second_destination = GatherDestination - 1,
        .flags = HalfOutput | NoDependency}.Assemble(),
};

constexpr Instruction MoveImmediate(u32 destination, u32 value) {
    return {{0x7802U | (destination << 16), value, 0xf00, 0}};
}

constexpr u32 PackOffsets(const std::array<s8, 4>& offsets) {
    u32 word{};
    for (u32 index = 0; index < offsets.size(); ++index) {
        word |= u32{static_cast<u8>(offsets[index])} << (index * 8);
    }
    return word;
}

constexpr auto TapOffsetsX = std::array<s8, 4>{-2, 1, 3, -5};
constexpr auto TapOffsetsY = std::array<s8, 4>{6, -7, 2, 4};
constexpr u32 GuardInput = 46;
constexpr u32 GuardedGatherDestination = 50;

constexpr std::array<Instruction, 6> GuardedGatherProgram(u32 guard, bool half) {
    constexpr u32 offsets_register = 54;
    constexpr u32 thread_id_x = 0x21;
    const Instruction thread_id{{0x7919U | (GuardInput << 16), 0, thread_id_x << 8, 0}};
    const Instruction compare{{0x720cU | (GuardInput << 24), NoRegister, 0x03f25070, 0}};
    // IMAD with both multiplicands RZ writes the immediate addend under the supplied predicate.
    const auto guarded_move = [&](u32 destination, u32 value) {
        return Instruction{{0x424U | (guard << 12) | (destination << 16) | (NoRegister << 24),
                            value, 0x078e00ff, 0}};
    };
    const auto gather = TextureEncoding{
        .opcode = 0xb63, .destination = GuardedGatherDestination, .coordinates = 62,
        .operand = offsets_register, .second_destination = GuardInput,
        .guard = guard, .flags = PerTap | (half ? HalfOutput : 0U)}.Assemble();
    return {thread_id, compare, guarded_move(offsets_register, PackOffsets(TapOffsetsX)),
            guarded_move(offsets_register + 1, PackOffsets(TapOffsetsY)), gather, Exit};
}

void KeepResult(std::vector<u64>& code, u32 reg) {
    constexpr u64 store_shared = 0xef58000000070000ULL;
    code.insert(code.end() - 1, store_shared | reg | (u64{NoRegister} << 8) |
                                   (u64{reg * sizeof(u32)} << 20) | (4ULL << 48));
}

void DumpSyntheticTextureModule(std::string_view name, std::span<const u32> code) {
    if (const char* directory = std::getenv("EDEN_SYNTHETIC_TEXTURE_TEST_OUTPUT")) {
        std::filesystem::create_directories(directory);
        const auto path = std::filesystem::path{directory} / (std::string{name} + ".spv");
        std::ofstream file{path, std::ios::binary};
        file.write(reinterpret_cast<const char*>(code.data()),
                   static_cast<std::streamsize>(code.size_bytes()));
        REQUIRE(file.good());
    }
}

class TextureEnvironment final : public Shader::Environment {
public:
    u32 binding{TextureBank};
    bool integer_format{false};
    std::span<const u64> code;

    TextureEnvironment() { stage = Shader::Stage::Compute; }

    u64 ReadInstruction(u32 address) override {
        for (u32 index = 0; index < code.size(); ++index) {
            if (address == 0x50 + index * 8 + ((index + 1) / 3) * 8) {
                return code[index];
            }
        }
        FAIL("Read outside synthetic texture fixture at " << address);
        return 0;
    }
    u32 ReadCbufValue(u32 bank, u32 offset) override { return (bank << 16) | offset; }
    Shader::TextureType ReadTextureType(u32) override { return Shader::TextureType::Color2D; }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return Shader::TexturePixelFormat::A8B8G8R8_UNORM;
    }
    bool IsTexturePixelFormatInteger(u32) override {
        return integer_format;
    }
    u32 ReadViewportTransformState() override { return 1; }
    u32 TextureBoundBuffer() const override { return binding; }
    u32 LocalMemorySize() const override { return 0; }
    u32 SharedMemorySize() const override { return 4096; }
    std::array<u32, 3> WorkgroupSize() const override { return {32, 1, 1}; }
    bool HasHLEMacroState() const override { return false; }
    bool HasNativeInstructions() const override { return true; }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}
};

u32 HalfFixture(u32 bits) {
    switch (bits) {
    case 0x00000000: return 0x0000;
    case 0x80000000: return 0x8000;
    case 0x3f800000: return 0x3c00;
    case 0xbf000000: return 0xb800;
    case 0x3e800000: return 0x3400;
    case 0x3f400000: return 0x3a00;
    case 0x3f801000: return 0x3c00;
    case 0x3f801001: return 0x3c01;
    default:
        FAIL("Unexpected half fixture " << bits);
        return 0;
    }
}

struct TextureTranslation {
    using Vector = std::array<u32, 4>;
    TextureEnvironment env;
    Shader::ObjectPool<IR::Inst> pool{256};
    IR::Block block{pool};
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    std::array<u32, IR::NUM_REGS> registers{};
    Vector sample{0x3f800000, 0xbf000000, 0x3e800000, 0x3f400000};
    Vector coordinates{};
    Vector offsets{}, offsets2{};
    u32 handle{TextureWord * sizeof(u32)};
    u32 component{};
    u32 level{};
    u32 textures{};

    void Translate(const Shader::Ampere::LoweredProgram& lowered, u32 guard) {
        REQUIRE(lowered.required_texture_buffer == env.TextureBoundBuffer());
        for (const u64 word : lowered.instructions) {
            if ((word >> 48) == 0xe300) {
                continue;
            }
            REQUIRE(((word >> 16) & 15) == ((word >> 56) == 0x01 ? 7U : guard));
            if ((word >> 48) == 0x5c98) {
                visitor.MOV_reg(word);
            } else if ((word >> 56) == 0x01) {
                visitor.MOV32I(word);
            } else if ((word >> 48) == 0x4c98) {
                visitor.MOV_cbuf(word);
            } else if ((word >> 48) == 0x5ca8) {
                visitor.F2F_reg(word);
            } else if ((word >> 48) == 0x5c18) {
                visitor.ISCADD_reg(word);
            } else if ((word >> 56) == 0xd2) {
                visitor.TLDS(word);
            } else if ((word >> 58) == 0x32) {
                visitor.TLD4(word);
            } else if ((word >> 56) == 0xde) {
                visitor.TEX_b(word);
            } else if ((word >> 60) == 0xc) {
                visitor.TEX(word);
            } else {
                FAIL("Unexpected lowered texture instruction " << word);
            }
        }
    }

    void Run() {
        std::unordered_map<const IR::Inst*, Vector> values;
        const auto read = [&](IR::Value value) -> Vector {
            if (!value.IsImmediate()) {
                return values.at(value.InstRecursive());
            }
            return {value.Type() == IR::Type::F32 ? std::bit_cast<u32>(value.F32()) : value.U32()};
        };
        for (const IR::Inst& inst : block) {
            const auto arg = [&](size_t i) { return read(inst.Arg(i))[0]; };
            Vector result{};
            switch (inst.GetOpcode()) {
            case IR::Opcode::GetRegister:
                result[0] = registers.at(static_cast<size_t>(inst.Arg(0).Reg()));
                break;
            case IR::Opcode::SetRegister:
                registers.at(static_cast<size_t>(inst.Arg(0).Reg())) = arg(1);
                break;
            case IR::Opcode::BitCastU32F32:
            case IR::Opcode::BitCastF32U32:
                result[0] = arg(0);
                break;
            case IR::Opcode::CompositeConstructU32x2:
            case IR::Opcode::CompositeConstructF32x2:
            case IR::Opcode::CompositeConstructF16x2:
                result = {arg(0), arg(1)};
                break;
            case IR::Opcode::CompositeConstructU32x4:
                result = {arg(0), arg(1), arg(2), arg(3)};
                break;
            case IR::Opcode::BitFieldSExtract:
                result[0] = static_cast<u32>(static_cast<s32>(arg(0) << (32 - arg(1) - arg(2))) >> (32 - arg(2)));
                break;
            case IR::Opcode::BoundImageFetch:
            case IR::Opcode::BoundImageGather:
                REQUIRE(arg(0) == handle);
                coordinates = read(inst.Arg(1));
                if (inst.GetOpcode() == IR::Opcode::BoundImageFetch) {
                    level = arg(3);
                    REQUIRE(inst.Flags<IR::TextureInstInfo>().relaxed_precision == 1);
                } else {
                    component = inst.Flags<IR::TextureInstInfo>().gather_component;
                    if (!inst.Arg(2).IsEmpty()) offsets = read(inst.Arg(2));
                    if (!inst.Arg(3).IsEmpty()) offsets2 = read(inst.Arg(3));
                }
                result = sample;
                ++textures;
                break;
            case IR::Opcode::CompositeExtractF32x4:
                result[0] = read(inst.Arg(0)).at(arg(1));
                break;
            case IR::Opcode::ConvertF16F32:
                if (inst.Arg(0).IsImmediate()) {
                    REQUIRE(inst.Arg(0).F32() == 0.0f);
                } else {
                    REQUIRE(inst.Flags<IR::FpControl>().rounding == IR::FpRounding::RN);
                }
                result[0] = HalfFixture(arg(0));
                break;
            case IR::Opcode::PackFloat2x16: {
                const auto pair{read(inst.Arg(0))};
                result[0] = pair[0] | (pair[1] << 16);
                break;
            }
            case IR::Opcode::PackHalf2x16: {
                const auto pair{read(inst.Arg(0))};
                result[0] = HalfFixture(pair[0]) | (HalfFixture(pair[1]) << 16);
                break;
            }
            case IR::Opcode::ShiftLeftLogical32:
                result[0] = arg(0) << arg(1);
                break;
            case IR::Opcode::IAdd32:
                result[0] = arg(0) + arg(1);
                break;
            default:
                FAIL("Unexpected translated texture opcode " << IR::NameOf(inst.GetOpcode()));
            }
            values.emplace(&inst, result);
        }
    }
};
}

TEST_CASE("Synthetic array depth samples preserve alternate descriptor banks",
          "[video_core][ampere][logged_textures]") {
    for (const u32 first_binding : {2U, 15U}) {
        auto first = DepthSample;
        first.bank = first_binding;
        const auto second = DepthSample.Assemble();
        const auto lowered{Shader::Ampere::LowerToMaxwell(
            std::array{first.Assemble(), second, second, Exit})};
        INFO(lowered.error);
        REQUIRE(lowered);
        TextureTranslation translated;
        translated.env.binding = first_binding;
        translated.Translate(lowered, 7);
        IR::Program program;
        program.blocks = {&translated.block};
        program.post_order_blocks = {&translated.block};
        Shader::Optimization::SsaRewritePass(program);
        Shader::HostTranslateInfo host;
        host.ApplyDescriptorLimitPolicy();
        Shader::Optimization::TexturePass(translated.env, program, host);
        REQUIRE(program.info.texture_descriptors.size() == 2);
        std::array<u32, 3> bindings{};
        size_t count{};
        for (const auto& inst : translated.block) {
            if (inst.GetOpcode() != IR::Opcode::ImageSampleDrefImplicitLod) {
                continue;
            }
            const auto flags{inst.Flags<IR::TextureInstInfo>()};
            REQUIRE(flags.is_depth == 1);
            REQUIRE(flags.type == Shader::TextureType::ColorArray2D);
            const auto& descriptor{program.info.texture_descriptors.at(flags.descriptor_index)};
            CHECK(descriptor.cbuf_offset == TextureWord * sizeof(u32));
            CHECK(descriptor.handle_program == ~0U);
            REQUIRE(count < bindings.size());
            bindings[count++] = descriptor.cbuf_index;
        }
        CHECK(count == 3);
        CHECK(bindings == std::array<u32, 3>{first_binding, TextureBank, TextureBank});
    }
}

TEST_CASE("Depth-compare samples keep a float image when the descriptor reads as integer",
          "[video_core][ampere][logged_textures]") {
    const auto lowered{Shader::Ampere::LowerToMaxwell(std::array{DepthSample.Assemble(), Exit})};
    INFO(lowered.error);
    REQUIRE(lowered);
    TextureTranslation translated;
    translated.env.binding = TextureBank;
    translated.env.integer_format = true;
    translated.Translate(lowered, 7);
    IR::Program program;
    program.blocks = {&translated.block};
    program.post_order_blocks = {&translated.block};
    Shader::Optimization::SsaRewritePass(program);
    Shader::HostTranslateInfo host;
    host.ApplyDescriptorLimitPolicy();
    Shader::Optimization::TexturePass(translated.env, program, host);
    REQUIRE(program.info.texture_descriptors.size() == 1);
    CHECK(program.info.texture_descriptors.front().is_depth);
    CHECK_FALSE(program.info.texture_descriptors.front().is_integer);
}

TEST_CASE("Synthetic half fetch writes odd scalar destinations",
          "[video_core][ampere][logged_textures]") {
    for (const u32 guard : {1U, 7U, 9U}) {
        auto instruction{Fetch};
        instruction.words[0] = (instruction.words[0] & ~0xf000U) | (guard << 12);
        const auto lowered{Shader::Ampere::LowerToMaxwell(std::array{instruction, Exit})};
        INFO(lowered.error);
        REQUIRE(lowered);
        TextureTranslation translated;
        translated.registers.fill(0xdeadbeef);
        translated.registers[FetchDestination] = 3;
        translated.registers[FetchCoordinates] = 10;
        translated.registers[FetchCoordinates + 1] = 20;
        const auto before{translated.registers};
        translated.Translate(lowered, guard);
        translated.Run();
        CHECK(translated.textures == 1);
        CHECK(translated.coordinates == TextureTranslation::Vector{10, 20});
        CHECK(translated.level == 3);
        CHECK(translated.registers[FetchDestination] == 0xb8003c00);
        CHECK(translated.registers[FetchSecondDestination] == 0x00003400);
        for (u32 reg = 0; reg < 246; ++reg) {
            if (reg != FetchDestination && reg != FetchSecondDestination) {
                REQUIRE(translated.registers[reg] == before[reg]);
            }
        }
    }
}

TEST_CASE("Depth sampling emits float image and result types for integer descriptors",
          "[video_core][spirv][depth_compare]") {
    for (const bool depth : {false, true}) {
        for (const bool integer : {false, true}) {
            CAPTURE(depth, integer);
            TextureEnvironment env;
            env.integer_format = integer;
            Shader::ObjectPool<IR::Inst> pool;
            IR::Block block{pool};
            IR::Program program;
            program.stage = Shader::Stage::Fragment;
            program.blocks = {&block};
            program.post_order_blocks = {&block};
            program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                                   {{}, IR::AbstractSyntaxNode::Type::Return}};
            IR::IREmitter ir{block};
            IR::TextureInstInfo flags{};
            flags.type.Assign(Shader::TextureType::Color2D);
            flags.is_depth.Assign(depth ? 1 : 0);
            const auto coords = ir.CompositeConstruct(ir.Imm32(0.0f), ir.Imm32(0.0f));
            const auto sample = depth
                ? IR::Value{ir.ImageSampleDrefImplicitLod(ir.Imm32(0), coords, ir.Imm32(0.5f),
                                                        {}, {}, {}, flags)}
                : ir.ImageSampleExplicitLod(ir.Imm32(0), coords, ir.Imm32(0.0f), {}, flags);
            Shader::HostTranslateInfo host;
            host.ApplyDescriptorLimitPolicy();
            Shader::Optimization::TexturePass(env, program, host);
            REQUIRE(program.info.texture_descriptors.size() == 1);
            CHECK(program.info.texture_descriptors[0].is_integer == (integer && !depth));
            CHECK(sample.Inst()->Flags<IR::TextureInstInfo>().is_integer ==
                  static_cast<u32>(integer && !depth));
            Shader::Profile profile;
            profile.supported_spirv = 0x00010500;
            const auto code = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
            std::unordered_map<u32, u32> scalar_types;
            u32 image_sampled_type{};
            u32 dref_result_type{};
            for (size_t offset = 5; offset < code.size();) {
                const u32 size = code[offset] >> 16;
                REQUIRE(size != 0);
                REQUIRE(offset + size <= code.size());
                const u32 opcode = code[offset] & 0xffffU;
                if (opcode == 21 || opcode == 22) {
                    scalar_types.emplace(code[offset + 1], opcode);
                } else if (opcode == 25) {
                    image_sampled_type = code[offset + 2];
                    CHECK(code[offset + 4] == static_cast<u32>(depth));
                } else if (opcode == 89) {
                    dref_result_type = code[offset + 1];
                }
                offset += size;
            }
            REQUIRE(image_sampled_type != 0);
            CHECK(scalar_types.at(image_sampled_type) == (integer && !depth ? 21 : 22));
            if (depth) {
                CHECK(dref_result_type == image_sampled_type);
            }
            if (const char* directory = std::getenv("EDEN_DEPTH_TEST_OUTPUT")) {
                std::filesystem::create_directories(directory);
                const auto name = std::string{depth ? "depth" : "color"} +
                                  (integer ? "-integer" : "-float") + ".spv";
                std::ofstream file{std::filesystem::path{directory} / name, std::ios::binary};
                file.write(reinterpret_cast<const char*>(code.data()),
                           static_cast<std::streamsize>(code.size() * sizeof(u32)));
                REQUIRE(file.good());
            }
        }
    }
}

TEST_CASE("Synthetic half gathers round and pack both destination orders",
          "[video_core][ampere][logged_textures]") {
    for (const auto& source : Gathers) {
        for (const u32 guard : {1U, 7U, 9U}) {
            auto instruction{source};
            instruction.words[0] = (instruction.words[0] & ~0xf000U) | (guard << 12);
            const auto lowered{Shader::Ampere::LowerToMaxwell(std::array{instruction, Exit})};
            INFO(lowered.error);
            REQUIRE(lowered);
            TextureTranslation translated;
            translated.registers.fill(0xdeadbeef);
            translated.registers[GatherCoordinates] = 0x3e000000;
            translated.registers[GatherCoordinates + 1] = 0x3f400000;
            translated.sample = {0x3f801000, 0x3f801001, 0x80000000, 0x3f400000};
            const auto before{translated.registers};
            translated.Translate(lowered, guard);
            translated.Run();
            CHECK(translated.textures == 1);
            CHECK(translated.coordinates == TextureTranslation::Vector{0x3e000000, 0x3f400000});
            CHECK(translated.registers[GatherDestination] == 0x3c013c00);
            CHECK(translated.registers[instruction.Bits(64, 8)] == 0x3a008000);
            for (u32 reg = 0; reg < 246; ++reg) {
                if (reg != GatherDestination && reg != instruction.Bits(64, 8)) {
                    REQUIRE(translated.registers[reg] == before[reg]);
                }
            }
        }
    }
}

TEST_CASE("Half gathers with fewer than four taps pack only the taps they have",
          "[video_core][ampere][logged_textures]") {
    struct Case {
        u32 mask;
        u32 second_dest;
        u32 first;
        std::optional<u32> second;
    };
    for (const Case& c : {Case{1, NoRegister, 0x00003c00, std::nullopt},
                          Case{2, NoRegister, 0x00003c01, std::nullopt},
                          Case{3, NoRegister, 0x3c013c00, std::nullopt},
                          Case{12, NoRegister, 0x3a008000, std::nullopt},
                          Case{3, GatherDestination + 1, 0x3c013c00, std::nullopt},
                          Case{7, GatherDestination + 1, 0x3c013c00, 0x00008000}}) {
        INFO("mask " << c.mask << " second destination " << c.second_dest);
        auto instruction{Gathers[0]};
        instruction.words[2] = (instruction.words[2] & ~0xfffU) | (c.mask << 8) | c.second_dest;
        const auto lowered{Shader::Ampere::LowerToMaxwell(std::array{instruction, Exit})};
        INFO(lowered.error);
        REQUIRE(lowered);
        TextureTranslation translated;
        translated.registers.fill(0xdeadbeef);
        translated.registers[GatherCoordinates] = 0x3e000000;
        translated.registers[GatherCoordinates + 1] = 0x3f400000;
        translated.registers[255] = 0; // RZ: constant propagation folds its reads to zero
        translated.sample = {0x3f801000, 0x3f801001, 0x80000000, 0x3f400000};
        const auto before{translated.registers};
        translated.Translate(lowered, 7);
        translated.Run();
        CHECK(translated.textures == 1);
        CHECK(translated.registers[GatherDestination] == c.first);
        if (c.second) {
            CHECK(translated.registers[c.second_dest] == *c.second);
        }
        for (u32 reg = 0; reg < 246; ++reg) {
            if (reg != GatherDestination && (!c.second || reg != c.second_dest)) {
                REQUIRE(translated.registers[reg] == before[reg]);
            }
        }
    }
    // Three or four taps still need a second destination register.
    for (const u32 mask : {7U, 15U}) {
        auto instruction{Gathers[0]};
        instruction.words[2] = (instruction.words[2] & ~0xfffU) | (mask << 8) | 0xff;
        CHECK_FALSE(Shader::Ampere::LowerToMaxwell(std::array{instruction, Exit}));
    }
}

TEST_CASE("Ampere half per-tap gathers round and pack each pair in low-high order",
          "[video_core][ampere][gather][logged_textures]") {
    const std::array forms{
        TextureEncoding{.opcode = 0xb63, .destination = 22, .coordinates = 40,
                        .operand = 34, .second_destination = 34, .bank = 9,
                        .flags = HalfOutput | PerTap | NoDependency},
        TextureEncoding{.opcode = 0xb63, .destination = 26, .coordinates = 48,
                        .operand = 4, .second_destination = 29, .table_word = 0x3a,
                        .flags = HalfOutput | PerTap},
    };
    for (const auto& form : forms) {
        for (const u32 component : {0U, 1U, 2U, 3U}) {
            for (const u32 guard : {1U, 7U, 9U}) {
                auto encoding = form;
                encoding.guard = guard;
                encoding.flags |= component << 23;
                const auto gather = encoding.Assemble();
                const u32 meta = form.operand;
                const std::array code{
                    MoveImmediate(meta, PackOffsets(TapOffsetsX)),
                    MoveImmediate(meta + 1, PackOffsets(TapOffsetsY)),
                    gather, Exit,
                };
                auto lowered = Shader::Ampere::LowerToMaxwell(code);
                INFO(lowered.error);
                REQUIRE(lowered);
                lowered.instructions.erase(lowered.instructions.begin(),
                    lowered.instructions.begin() + lowered.source_instruction_offsets[2]);
                TextureTranslation translated;
                translated.env.binding = lowered.required_texture_buffer;
                translated.handle = form.table_word * sizeof(u32);
                translated.registers.fill(0xdeadbeef);
                translated.registers[form.coordinates] = 0x3e000000;
                translated.registers[form.coordinates + 1] = 0x3f400000;
                translated.sample = {0x3f801000, 0x3f801001, 0x80000000, 0x3f400000};
                const auto before = translated.registers;
                translated.Translate(lowered, guard);
                translated.Run();
                CHECK(translated.textures == 1);
                CHECK(translated.component == component);
                CHECK(translated.coordinates == TextureTranslation::Vector{0x3e000000, 0x3f400000});
                CHECK(translated.offsets ==
                      TextureTranslation::Vector{0xfffffffe, 1, 3, 0xfffffffb});
                CHECK(translated.offsets2 == TextureTranslation::Vector{6, 0xfffffff9, 2, 4});
                CHECK(translated.registers[form.destination] == 0x3c013c00);
                CHECK(translated.registers[form.second_destination] == 0x3a008000);
                for (u32 reg = 0; reg < 246; ++reg) {
                    if (reg != form.destination && reg != form.second_destination)
                        REQUIRE(translated.registers[reg] == before[reg]);
                }
                for (const u32 rounding : {2U, 3U}) {
                    auto invalid = code;
                    invalid[2].words[2] = (invalid[2].words[2] & ~(3U << 15)) | (rounding << 15);
                    CHECK_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
                }
            }
        }
    }
}

TEST_CASE("Synthetic half textures retain unsupported-mode rejections",
          "[video_core][ampere][logged_textures]") {
    for (const u32 bit : {76U, 77U, 78U, 80U, 91U}) {
        auto invalid{Gathers[0]};
        invalid.words[bit / 32] ^= 1U << (bit % 32);
        CHECK_FALSE(Shader::Ampere::LowerToMaxwell(std::array{invalid, Exit}));
    }
    auto invalid{Fetch};
    invalid.words[2] &= ~(1U << 15);
    CHECK_FALSE(Shader::Ampere::LowerToMaxwell(std::array{invalid, Exit}));
}

TEST_CASE("Synthetic half textures reach production SPIRV emission",
          "[video_core][ampere][logged_textures]") {
    for (const auto instruction : {Fetch, Gathers[0], Gathers[1]}) {
        const auto lowered{Shader::Ampere::LowerToMaxwell(std::array{instruction, Exit})};
        INFO(lowered.error);
        REQUIRE(lowered);
        auto code{lowered.instructions};
        for (const u32 source : {instruction.Bits(16, 8), instruction.Bits(64, 8)}) {
            KeepResult(code, source);
        }
        TextureEnvironment env;
        env.code = code;
        env.binding = lowered.required_texture_buffer;
        Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
        Shader::Maxwell::Flow::CFG cfg{env, flow_pool, Shader::Maxwell::Location{0x50}};
        Shader::ObjectPool<IR::Inst> inst_pool;
        Shader::ObjectPool<IR::Block> block_pool;
        Shader::HostTranslateInfo host;
        host.ApplyDescriptorLimitPolicy();
        auto program{Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg, host)};
        u32 stores{};
        for (const auto* block : program.blocks) {
            for (const auto& inst : *block) {
                stores += inst.GetOpcode() == IR::Opcode::WriteSharedU32;
            }
        }
        REQUIRE(stores == 2);
        Shader::Profile profile{};
        profile.supported_spirv = 0x00010400;
        const auto spirv{Shader::Backend::SPIRV::EmitSPIRV(profile, program)};
        REQUIRE(spirv.size() > 5);
        CHECK(spirv.front() == 0x07230203);
        DumpSyntheticTextureModule("half-" + std::to_string(instruction.Bits(64, 8)), spirv);
    }
}

TEST_CASE("Guarded per-tap gathers retain immediate offsets through SPIRV translation",
          "[video_core][ampere][gather][logged_textures]") {
    for (const bool half : {false, true}) {
        for (const u32 guard : {1U, 9U}) {
            const auto native = GuardedGatherProgram(guard, half);
            const auto lowered = Shader::Ampere::LowerToMaxwell(native);
            INFO(lowered.error);
            REQUIRE(lowered);
            auto code = lowered.instructions;
            for (const u32 reg : {GuardInput, GuardedGatherDestination}) {
                KeepResult(code, reg);
            }
            TextureEnvironment env;
            env.code = code;
            env.binding = lowered.required_texture_buffer;
            Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
            Shader::Maxwell::Flow::CFG cfg{env, flow_pool, Shader::Maxwell::Location{0x50}};
            Shader::ObjectPool<IR::Inst> inst_pool;
            Shader::ObjectPool<IR::Block> block_pool;
            Shader::HostTranslateInfo host;
            host.ApplyDescriptorLimitPolicy();
            auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg, host);
            u32 gathers = 0;
            for (const auto* block : program.blocks) {
                for (const auto& inst : *block) {
                    if (inst.GetOpcode() != IR::Opcode::ImageGather) continue;
                    ++gathers;
                    for (const u32 argument : {2U, 3U}) {
                        const auto* offsets = inst.Arg(argument).InstRecursive();
                        REQUIRE(offsets->GetOpcode() == IR::Opcode::CompositeConstructU32x4);
                        REQUIRE(offsets->AreAllArgsImmediates());
                        const auto& expected = argument == 2 ? TapOffsetsX : TapOffsetsY;
                        for (size_t tap = 0; tap < expected.size(); ++tap) {
                            CHECK(offsets->Arg(tap).U32() == static_cast<u32>(expected[tap]));
                        }
                    }
                }
            }
            REQUIRE(gathers == 1);
            Shader::Profile profile{};
            profile.supported_spirv = 0x00010400;
            const auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
            REQUIRE(spirv.size() > 5);
            CHECK(spirv.front() == 0x07230203);
            DumpSyntheticTextureModule("guard-" + std::to_string(guard) +
                                           (half ? "-half" : "-float"),
                                       spirv);
        }
    }
}

TEST_CASE("Half gathers reject reserved second coordinates before scratch copies",
          "[video_core][ampere][logged_textures]") {
    for (u32 source = 246; source < 255; ++source) {
        auto instruction{Gathers[0]};
        instruction.words[1] = (instruction.words[1] & ~255U) | source;
        CAPTURE(source);
        CHECK_FALSE(Shader::Ampere::LowerToMaxwell(std::array{instruction, Exit}));
    }
    for (const u32 source : {0U, 1U, 215U, 245U, 255U}) {
        auto instruction{Gathers[0]};
        instruction.words[1] = (instruction.words[1] & ~255U) | source;
        const auto lowered{Shader::Ampere::LowerToMaxwell(std::array{instruction, Exit})};
        REQUIRE(lowered);
        TextureTranslation translated;
        translated.registers[GatherCoordinates] = 0x3e000000;
        if (source != 255) {
            translated.registers[source] = 0x3f400000;
        }
        translated.Translate(lowered, 7);
        translated.Run();
        CHECK(translated.coordinates ==
              TextureTranslation::Vector{0x3e000000, source == 255 ? 0U : 0x3f400000U});
        CHECK(translated.registers[GatherDestination] == 0xb8003c00);
        CHECK(translated.registers[GatherDestination + 1] == 0x3a003400);
    }
    constexpr Instruction uniform_move{{0x00007882, 0, 0, 0}};
    for (const u32 high_uniforms : {0U, 4U, 8U}) {
        const u32 limit = 216 - high_uniforms;
        for (const u32 source : {limit - 1, limit, 245U, 250U, 254U, 255U}) {
            auto instruction{Gathers[0]};
            instruction.words[1] = (instruction.words[1] & ~255U) | source;
            const auto lowered{Shader::Ampere::LowerToMaxwellWith(
                std::array{uniform_move, instruction, Exit}, high_uniforms, false, false)};
            CAPTURE(high_uniforms, source);
            CHECK(static_cast<bool>(lowered) == (source < limit || source == 255));
        }
    }
}

TEST_CASE("Half per-tap gathers prove offsets across an unrelated LEA.HI",
          "[video_core][ampere][gather][logged_textures]") {
    constexpr u32 offsets_register = 12;
    constexpr u32 lea_low = 24;
    constexpr u32 lea_addend = 19;
    constexpr u32 lea_shift = 27;
    constexpr Instruction lea{{0x7211U | (lea_low << 16) | (lea_low << 24), lea_addend,
                              0x078f0000U | (lea_shift << 11) | NoRegister, 0}};
    constexpr auto gather = TextureEncoding{
        .opcode = 0xb63, .destination = 38, .coordinates = 60,
        .operand = offsets_register, .second_destination = 33,
        .flags = HalfOutput | PerTap}.Assemble();
    auto code = std::array{
        MoveImmediate(offsets_register, PackOffsets(TapOffsetsX)),
        MoveImmediate(offsets_register + 1, PackOffsets(TapOffsetsY)),
        lea, gather, Exit,
    };
    const auto lowered = Shader::Ampere::LowerToMaxwell(code);
    INFO(lowered.error);
    REQUIRE(lowered);
    code[2].words[0] = (code[2].words[0] & ~(255U << 16)) | ((offsets_register + 1) << 16);
    CHECK(Shader::Ampere::LowerToMaxwell(code).error ==
          "native per-tap gather nonconstant offsets");
}

TEST_CASE("Per-tap gathers with runtime offsets gather each tap at its own offset",
          "[video_core][ampere][gather][logged_textures]") {
    const auto native = GuardedGatherProgram(7, false);
    const auto lowered = Shader::Ampere::LowerToMaxwell(native);
    INFO(lowered.error);
    REQUIRE(lowered);
    auto code = lowered.instructions;
    // Replace both proven offsets with reads from different words in a synthetic constant buffer.
    const auto offsets_end = code.begin() + lowered.source_instruction_offsets[4] + 2;
    REQUIRE((*(offsets_end - 1) & 0xff) == 249);
    constexpr u32 first_offset_word = 0x68;
    const auto runtime_offset = [](u32 reg, u32 word) {
        return 0x4c98000000070000ULL | reg | (u64{15} << 39) | (u64{word} << 20);
    };
    code.insert(offsets_end, {runtime_offset(248, first_offset_word),
                             runtime_offset(249, first_offset_word + 1)});
    for (const u32 reg : {GuardInput, GuardedGatherDestination}) {
        KeepResult(code, reg);
    }
    TextureEnvironment env;
    env.code = code;
    env.binding = lowered.required_texture_buffer;
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, Shader::Maxwell::Location{0x50}};
    Shader::ObjectPool<IR::Inst> inst_pool;
    Shader::ObjectPool<IR::Block> block_pool;
    Shader::HostTranslateInfo host;
    host.ApplyDescriptorLimitPolicy();
    auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg, host);
    u32 gathers = 0;
    std::array<bool, 2> offset_words_read{};
    for (const auto* block : program.blocks) {
        for (const auto& inst : *block) {
            if (inst.GetOpcode() == IR::Opcode::GetCbufU32 && inst.Arg(0).IsImmediate() &&
                inst.Arg(0).U32() == 0 && inst.Arg(1).IsImmediate()) {
                for (size_t index = 0; index < offset_words_read.size(); ++index) {
                    offset_words_read[index] |= inst.Arg(1).U32() ==
                                                (first_offset_word + index) * sizeof(u32);
                }
            }
            if (inst.GetOpcode() != IR::Opcode::ImageGather) continue;
            ++gathers;
            REQUIRE_FALSE(inst.Arg(2).InstRecursive()->AreAllArgsImmediates());
            REQUIRE_FALSE(inst.Arg(3).InstRecursive()->AreAllArgsImmediates());
        }
    }
    REQUIRE(gathers == 1);
    CHECK(offset_words_read == std::array<bool, 2>{true, true});
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010400;
    const auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
    REQUIRE(spirv.size() > 5);
    constexpr u32 OpImageGather = 96;
    constexpr u32 OffsetMask = 0x10, ConstOffsetsMask = 0x20;
    u32 offset_gathers = 0;
    for (size_t i = 5; i < spirv.size();) {
        const u32 count = spirv[i] >> 16;
        REQUIRE(count > 0);
        if ((spirv[i] & 0xffff) == OpImageGather) {
            REQUIRE(count > 6);
            CHECK((spirv[i + 6] & ConstOffsetsMask) == 0);
            if ((spirv[i + 6] & OffsetMask) != 0) ++offset_gathers;
        }
        i += count;
    }
    CHECK(offset_gathers == 4);
    DumpSyntheticTextureModule("runtime-offsets", spirv);
}
