// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;

constexpr u32 Always = 7;
constexpr u32 ZeroRegister = 255;
constexpr u32 MinMaxInput = 18;
constexpr u32 MinMaxOutput = 27;
constexpr u32 SurfaceCoordinate = 19;
constexpr u32 SurfaceData = 29;
constexpr u32 SurfaceBinding = 6;
constexpr u32 SurfaceWord = 11;

constexpr u32 Opcode(u32 opcode, u32 predicate = Always, u32 dest = 0, u32 source = 0) {
    return opcode | (predicate << 12) | (dest << 16) | (source << 24);
}

constexpr Instruction Exit{{Opcode(0x94d), 0, Always << 23, 0}};

constexpr Instruction MinMax(u32 value, u32 guard = Always, u32 selector = Always,
                             bool is_signed = false) {
    return {{Opcode(0x817, guard, MinMaxOutput, MinMaxInput), value,
             (selector << 23) | (u32{is_signed} << 9), 0}};
}

constexpr Instruction Reduce(u32 dimension, u32 operation = 0,
                             u32 coordinate = SurfaceCoordinate, u32 data = SurfaceData,
                             u32 guard = Always) {
    // SURED.D.STRONG.GPU.IGN with an immediate descriptor and 32-bit data.
    return {{Opcode(0x59f, guard, 0, coordinate),
             data | (SurfaceWord << 8) | (SurfaceBinding << 22) | (dimension << 29),
             (Always << 13) | (1U << 20) | (operation << 23), 0}};
}

constexpr Instruction OffsetGather(u32 coordinate) {
    // Two coordinates in Ra and Ra+1, offsets in R31, and a handle in UR9.
    return {{Opcode(0xf63, Always, 45, coordinate), 31U | (9U << 8) | (3U << 28),
             49U | (15U << 8) | (1U << 12) | (Always << 17) | (1U << 26) | (1U << 27), 0}};
}

constexpr Instruction MoveImmediate(u32 dest, u32 value) {
    return {{Opcode(0x802, Always, dest), value, 15U << 8, 0}};
}
}

TEST_CASE("Ampere IMNMX preserves full-width immediates", "[ampere][yoshi]") {
    for (const u32 value : {0x80000U, 0x23456789U, 0x7fffffffU, 0x80000000U, 0xfff7ffffU}) {
        for (const u32 guard : {0U, 7U, 9U}) {
            for (const u32 selector : {Always, 15U}) {
                for (const bool is_signed : {false, true}) {
                    const auto lowered =
                        LowerToMaxwell(std::array{MinMax(value, guard, selector, is_signed), Exit});
                    INFO(lowered.error);
                    REQUIRE(lowered);
                    REQUIRE(lowered.instructions.size() == 3);
                    CHECK(lowered.instructions[0] ==
                          (0x010000000000f0fbULL | (u64{guard} << 16) | (u64{value} << 20)));
                    CHECK(lowered.instructions[1] ==
                          (0x5c2000000fb00000ULL | MinMaxOutput | (u64{MinMaxInput} << 8) |
                           (u64{guard} << 16) | (u64{selector} << 39) | (u64{is_signed} << 48)));
                    CHECK(lowered.source_size == 32);
                }
            }
        }
    }
}

TEST_CASE("Ampere IMNMX retains its signed twenty-bit immediate encoding", "[ampere][yoshi]") {
    for (const u32 value : {0U, 0x7ffffU, 0xfff80000U, 0xffffffffU}) {
        const auto lowered = LowerToMaxwell(std::array{MinMax(value), Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        REQUIRE(lowered.instructions.size() == 2);
        CHECK(lowered.instructions[0] ==
              (0x3820038000070000ULL | MinMaxOutput | (u64{MinMaxInput} << 8) |
               (u64{value & 0x7ffffU} << 20) |
               (u64{value >> 31} << 56)));
    }
    auto unsupported = MinMax(0x3456789aU);
    unsupported.words[2] |= 0x400;
    REQUIRE_FALSE(LowerToMaxwell(std::array{unsupported, Exit}));
}

TEST_CASE("Ampere SURED preserves its dimension and descriptor", "[ampere][yoshi]") {
    for (const u32 dimension : {1U, 3U, 5U}) {
        for (const u32 guard : {0U, 7U, 9U}) {
            const auto instruction = Reduce(dimension, 0, SurfaceCoordinate, SurfaceData, guard);
            const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
            INFO(lowered.error);
            REQUIRE(lowered);
            REQUIRE(lowered.instructions.size() == 2);
            CHECK(lowered.required_texture_buffer == SurfaceBinding);
            CHECK(lowered.instructions[0] ==
                  (0xeb48000000000000ULL | SurfaceData | (u64{SurfaceCoordinate} << 8) |
                   (u64{guard} << 16) | (u64{dimension} << 33) | (u64{SurfaceWord} << 36)));
        }
    }
    for (const u32 dimension : {0U, 2U, 4U, 6U, 7U}) {
        REQUIRE_FALSE(LowerToMaxwell(std::array{Reduce(dimension), Exit}));
    }
    auto unsupported = Reduce(1);
    unsupported.words[2] |= 0x200;
    REQUIRE_FALSE(LowerToMaxwell(std::array{unsupported, Exit}));
}

TEST_CASE("Ampere bindless gathers lower with their offset mode and predicate",
          "[ampere][yoshi]") {
    struct Case {
        Instruction instruction;
        u32 predicate;
        u32 offsets;
    };
    // The register-handle form packs coordinates in Ra and Ra+1 and uses Rb for the handle.
    constexpr Instruction register_gather{{Opcode(0x364, 1, 54, 32), 36U | (7U << 27),
                                           57U | (15U << 8) | (Always << 17), 0}};
    for (const auto& sample :
         std::array{Case{OffsetGather(22), Always, 1}, Case{register_gather, 1, 0}}) {
        CAPTURE(sample.instruction.Bits(0, 12));
        const auto lowered = LowerToMaxwell(std::array{sample.instruction, Exit});
        INFO(lowered.error << " at " << lowered.error_offset);
        REQUIRE(lowered);
        CHECK(lowered.source_size == 2 * sizeof(Instruction));
        const auto gather = std::find_if(lowered.instructions.begin(), lowered.instructions.end(),
            [](u64 word) { return (word & 0xffc0000000000000ULL) == 0xdec0000000000000ULL; });
        REQUIRE(gather != lowered.instructions.end());
        CHECK(((*gather >> 16) & 0xf) == sample.predicate);
        CHECK(((*gather >> 28) & 7) == 2); // 2D
        CHECK(((*gather >> 31) & 0xf) == 15);
        CHECK(((*gather >> 36) & 3) == sample.offsets);
    }
}

TEST_CASE("Ampere 3D surface reduction keeps its OR operation and descriptor",
          "[ampere][yoshi]") {
    constexpr u32 bitwise_or = 6;
    const auto lowered = LowerToMaxwell(std::array{Reduce(5, bitwise_or), Exit});
    INFO(lowered.error << " at " << lowered.error_offset);
    REQUIRE(lowered);
    REQUIRE(lowered.instructions.size() == 2);
    CHECK(lowered.required_texture_buffer == SurfaceBinding);
    const u64 reduction = lowered.instructions.front();
    CHECK((reduction & 0xfff0000000000000ULL) == 0xeb40000000000000ULL);
    CHECK(((reduction >> 24) & 7) == 6); // OR
    CHECK(((reduction >> 33) & 7) == 5); // 3D
    CHECK((reduction & 0xff) == SurfaceData);
    CHECK(((reduction >> 8) & 0xff) == SurfaceCoordinate);
    CHECK(((reduction >> 36) & 0x1fff) == SurfaceWord);
}

TEST_CASE("Ampere attribute load lowers while geometry exit state remains live",
          "[ampere][yoshi]") {
    constexpr u32 handler = 12;
    constexpr u32 load_index = 9;
    constexpr u32 vertex = 13;
    constexpr u32 destination = 34;
    constexpr u32 address = 0xa4;
    constexpr u32 components = 4;
    // The parser requires these exact installation and finalisation operations.
    constexpr std::array code{
        Instruction{{Opcode(0x34e), 0, 0, 0}},
        MoveImmediate(3, handler * sizeof(Instruction)),
        Instruction{{Opcode(0x210), 3, ZeroRegister | (15U << 13) | (127U << 20), 0}},
        Instruction{{Opcode(0x224, Always, 1, ZeroRegister), ZeroRegister,
                     1U | (1U << 10) | (Always << 17), 0}},
        Instruction{{Opcode(0x357), 0, 0, 0}},
        // Discard the setup values for R0, R1 and P0, leaving R3 live across the load.
        MoveImmediate(0, 0x21),
        MoveImmediate(1, 0x47),
        Instruction{{Opcode(0x20c, Always, 0, 1), ZeroRegister,
                     (2U << 12) | (Always << 20) | (Always << 23), 0}},
        MoveImmediate(vertex, 2),
        Instruction{{Opcode(0x321, Always, destination, ZeroRegister), vertex | (address << 8),
                     (components - 1) << 10, 0}},
        MoveImmediate(3, 0x16),
        Exit,
        Instruction{{Opcode(0x324, Always, ZeroRegister), ZeroRegister, 0, 0}},
        Instruction{{Opcode(0x957), 0, 0, 0}},
        Exit,
    };
    const auto lowered = LowerToMaxwell(code, true);
    INFO(lowered.error << " at " << lowered.error_offset);
    REQUIRE(lowered);
    CHECK(lowered.source_size == sizeof(code));
    const u64 load = lowered.instructions[lowered.source_instruction_offsets[load_index]];
    CHECK((load & 0xfff8000000000000ULL) == 0xefd8000000000000ULL);
    CHECK((load & 0xff) == destination);
    CHECK(((load >> 20) & 0x3ff) == address);
    CHECK(((load >> 39) & 0xff) == vertex);
    CHECK(((load >> 47) & 3) == components - 1);
}

TEST_CASE("Yoshi 3D surface coordinates stay outside lowering scratch registers",
          "[ampere][yoshi]") {
    const Instruction uniform_marker{{Opcode(0xc02, 15), 0, 0, 0}};
    const auto reduction = [](u32 coordinate) { return Reduce(5, 0, coordinate); };
    const auto plain = LowerToMaxwell(std::array{reduction(243), Exit});
    INFO(plain.error);
    REQUIRE(plain);
    CHECK(((plain.instructions.front() >> 8) & 0xff) == 243);
    const auto plain_overlap = LowerToMaxwell(std::array{reduction(244), Exit});
    REQUIRE_FALSE(plain_overlap);
    CHECK(plain_overlap.error == "native surface reduction mode");

    const auto uniform = LowerToMaxwell(std::array{uniform_marker, reduction(213), Exit});
    INFO(uniform.error);
    REQUIRE(uniform);
    CHECK(((uniform.instructions[uniform.source_instruction_offsets[1]] >> 8) & 0xff) == 213);
    const auto uniform_overlap =
        LowerToMaxwell(std::array{uniform_marker, reduction(214), Exit});
    REQUIRE_FALSE(uniform_overlap);
    CHECK(uniform_overlap.error == "native surface reduction mode");
}

TEST_CASE("Yoshi 3D surface register shadows include the depth coordinate",
          "[ampere][yoshi]") {
    auto instruction = Reduce(5, 0, 218);
    CHECK_FALSE(Shader::Ampere::PrepareRegisterPressure(instruction, 216).valid);

    instruction = Reduce(5, 0, 220);
    const auto pressure = Shader::Ampere::PrepareRegisterPressure(instruction, 216);
    REQUIRE(pressure.valid);
    const u32 coordinate = pressure.instruction.Bits(24, 8);
    CHECK(coordinate + 3 <= 216);
    for (u32 i = 0; i < 3; ++i) {
        CHECK(std::find(pressure.prefix.begin(), pressure.prefix.end(),
                        Shader::Ampere::NativeRegisterCopy(0, coordinate + i, 1, 220 + i)) !=
              pressure.prefix.end());
    }

    // Shadowing the data must not borrow the low quartet containing the third coordinate.
    instruction = Reduce(5, 0, 210, 240);
    const auto data_pressure = Shader::Ampere::PrepareRegisterPressure(instruction, 216);
    REQUIRE(data_pressure.valid);
    CHECK(data_pressure.instruction.Bits(24, 8) == 210);
    const u32 data = data_pressure.instruction.Bits(32, 8);
    CHECK(data / 4 != 212 / 4);
}

TEST_CASE("Ampere buffer reductions use only one coordinate register", "[ampere][yoshi]") {
    auto instruction = Reduce(1, 0, 245);
    const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    CHECK(((lowered.instructions.front() >> 8) & 0xff) == 245);

    instruction = Reduce(1, 0, 215);
    const auto pressure = Shader::Ampere::PrepareRegisterPressure(instruction, 216);
    REQUIRE(pressure.valid);
    CHECK(pressure.prefix.empty());
    CHECK(pressure.instruction.Bits(24, 8) == 215);
}

TEST_CASE("Yoshi bindless offset gather coordinates stay below the uniform bank",
          "[ampere][yoshi]") {
    const Instruction uniform_marker{{Opcode(0xc02, 15), 0, 0, 0}};
    const auto inside = LowerToMaxwell(std::array{uniform_marker, OffsetGather(214), Exit});
    INFO(inside.error);
    REQUIRE(inside);
    const auto crossing = LowerToMaxwell(std::array{uniform_marker, OffsetGather(215), Exit});
    REQUIRE_FALSE(crossing);
    CHECK(crossing.error == "native texture gather mode");
}

TEST_CASE("Ampere half immediate conversions lower only round to nearest", "[ampere][yoshi]") {
    Instruction conversion{{Opcode(0x906, Always, 53), 0x1a5, (1U << 11) | (1U << 21), 0}};
    const auto nearest = LowerToMaxwell(std::array{conversion, Exit});
    INFO(nearest.error);
    REQUIRE(nearest);
    const auto convert = std::find_if(nearest.instructions.begin(), nearest.instructions.end(),
        [](u64 word) { return (word & 0xfff8000000000000ULL) == 0x5cb8000000000000ULL; });
    REQUIRE(convert != nearest.instructions.end());
    CHECK(((*convert >> 8) & 3) == 1); // F16
    CHECK(((*convert >> 39) & 3) == 0);
    for (const u32 rounding : {1U, 2U, 3U}) {
        conversion.words[2] = (1U << 11) | (rounding << 14) | (1U << 21);
        REQUIRE_FALSE(LowerToMaxwell(std::array{conversion, Exit}));
    }
}
