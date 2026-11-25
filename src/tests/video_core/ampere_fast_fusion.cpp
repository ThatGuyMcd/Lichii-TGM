// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
constexpr Instruction Exit{{0x794d, 0, 0x03800000, 0}};
u64 Copy(u32 dest, u32 source, u32 guard = 7) {
    return 0x5c98000000000000ULL | (u64{guard} << 16) | dest |
           (15ULL << 39) | (u64{source} << 20);
}

constexpr Instruction UniformHandleFetch(u32 destination, u32 second, u32 coordinate_a,
                                         u32 coordinate_b, u32 handle, u32 mask) {
    constexpr u32 descriptor_layout = (1U << 28) | (1U << 29);
    constexpr u32 immediate_level_zero = (7U << 17) | (1U << 23) | (1U << 27);
    return {{0xf66U | (7U << 12) | (destination << 16) | (coordinate_a << 24),
             coordinate_b | (handle << 8) | descriptor_layout,
             second | (mask << 8) | immediate_level_zero, 0}};
}
} // namespace

TEST_CASE("Fast Fusion TXD allows an odd single third destination", "[ampere][fast_fusion]") {
    constexpr u32 destination = 18;
    constexpr u32 third = 31;
    constexpr u32 binding = 5;
    constexpr u32 descriptor = 0x21;
    constexpr u32 dimension_2d = 1;
    Instruction txd{{0xb6cU | (7U << 12) | (destination << 16) | (35U << 24),
                     43U | (descriptor << 8) | (binding << 22) | (dimension_2d << 29),
                     third | (7U << 8) | (15U << 17) | (1U << 26), 0}};
    const auto lowered = LowerToMaxwell(std::array{txd, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == binding);
    REQUIRE(lowered.instructions.size() == 5);
    CHECK((lowered.instructions[0] & 255) == 248);
    CHECK(lowered.instructions[1] == Copy(destination, 248));
    CHECK(lowered.instructions[2] == Copy(destination + 1, 249));
    CHECK(lowered.instructions[3] == Copy(third, 250));
    txd.words[2] |= 0x800; // Four channels still require an aligned second pair.
    CHECK_FALSE(LowerToMaxwell(std::array{txd, Exit}));
}

TEST_CASE("Fast Fusion formatted stores preserve channel masks and barriers", "[ampere][fast_fusion]") {
    constexpr u32 address = 21;
    constexpr u32 source = 33;
    constexpr u32 format_and_resource = 0x60005900;
    constexpr u32 opcode = 0xb9bU | (7U << 12) | (address << 24);
    for (u32 mask = 1; mask < 16; ++mask) {
        for (bool strong : {false, true}) {
            const Instruction store{{opcode, format_and_resource | source,
                                      (strong ? 0x0010a000U : 0x00100000U) | (mask << 8), 0}};
            const auto lowered = LowerToMaxwell(std::array{store, Exit});
            INFO(lowered.error);
            REQUIRE(lowered);
            REQUIRE(lowered.instructions.size() == (strong ? 4U : 2U));
            const auto encoded = lowered.instructions[strong ? 1 : 0];
            CHECK(((encoded >> 20) & 15) == mask);
            CHECK((encoded & 255) == source);
            CHECK(((encoded >> 8) & 255) == address);
            if (strong) CHECK(lowered.instructions[0] == lowered.instructions[2]);
        }
    }
    CHECK_FALSE(LowerToMaxwell(
        std::array{Instruction{{opcode, format_and_resource | source, 0x0010a000, 0}}, Exit}));
}

TEST_CASE("Fast Fusion uniform shifts borrow and restore the guard predicate", "[ampere][fast_fusion]") {
    for (u32 guard : {0U, 3U, 6U, 8U, 11U, 14U}) {
        constexpr u32 destination = 15, value = 12, amount = 8;
        const Instruction shift{{0x299U | (guard << 12) | (destination << 16) | (63U << 24),
                                 amount, 0x08011600 | value, 0}};
        const auto lowered = LowerToMaxwell(std::array{shift, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        REQUIRE(lowered.instructions.size() == 5);
        const u32 slot = 6 - (guard & 7), mask = 1U << slot;
        CHECK(lowered.instructions[0] == (0x38e8000000070000ULL | 253 | (255ULL << 8) | (u64{mask} << 20)));
        CHECK(lowered.instructions[1] == (0x38f0000000070000ULL | (239ULL << 8) | (u64{mask} << 20)));
        CHECK(lowered.instructions[2] == (0x5c28000000000000ULL | (u64{(guard & 8) | slot} << 16) |
                                          (216 + destination) | (u64{216 + value} << 8) |
                                          (u64{216 + amount} << 20)));
        CHECK(lowered.instructions[3] == (0x38f0000000070000ULL | (253ULL << 8) | (u64{mask} << 20)));
    }
}

TEST_CASE("Fast Fusion uniform descriptor fetch snapshots inputs and splits results", "[ampere][fast_fusion]") {
    const std::array programs{
        UniformHandleFetch(18, 31, 21, 31, 9, 7),
        UniformHandleFetch(30, 255, 16, 19, 11, 1),
        UniformHandleFetch(43, 255, 47, 52, 13, 1),
        UniformHandleFetch(23, 255, 23, 48, 9, 1),
    };
    for (auto fetch : programs) {
        const auto lowered = LowerToMaxwell(std::array{fetch, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        CHECK(lowered.instructions[0] == Copy(250, fetch.Bits(24, 8)));
        CHECK(lowered.instructions[1] == Copy(251, fetch.Bits(32, 8)));
        CHECK(lowered.instructions[2] == Copy(246, 216 + fetch.Bits(40, 6)));
        CHECK(((lowered.instructions[3] >> 31) & 15) == fetch.Bits(72, 4));
        CHECK(lowered.instructions[4] == Copy(fetch.Bits(16, 8), 250));
        if (fetch.Bits(72, 4) == 7) {
            CHECK(lowered.instructions[5] == Copy(fetch.Bits(16, 8) + 1, 251));
            CHECK(lowered.instructions[6] == Copy(fetch.Bits(64, 8), 252));
        }
        fetch.words[2] |= 1U << 12; // Offsets are not silently ignored.
        CHECK_FALSE(LowerToMaxwell(std::array{fetch, Exit}));
    }
}

TEST_CASE("Fast Fusion U64 left shift uses the exact high word through shift 32", "[ampere][fast_fusion]") {
    constexpr u32 high = 81, low = 82;
    const auto make_shift = [](u32 amount) {
        return Instruction{{0x819U | (7U << 12) | (high << 16) | (low << 24),
                            amount, (1U << 16) | (1U << 9) | high, 0}};
    };
    for (u32 shift = 0; shift <= 32; ++shift) {
        const auto lowered = LowerToMaxwell(std::array{make_shift(shift), Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        CHECK(lowered.instructions[0] == (0x36f8000000070000ULL | high | (u64{low} << 8) |
                                          (u64{high} << 39) | (u64{shift} << 20)));
    }
    CHECK_FALSE(LowerToMaxwell(std::array{make_shift(33), Exit}));
}

TEST_CASE("Fast Fusion constant LEA handles negation and high-word carry", "[ampere][fast_fusion]") {
    constexpr u32 low_register = 19, offset = 0x6f;
    const Instruction low{{0xa11U | (7U << 12) | (low_register << 16) | (low_register << 24),
                           0x80000000U | (offset << 8), 0x078e18ff, 0}};
    const auto lowered = LowerToMaxwell(std::array{low, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    REQUIRE(lowered.instructions.size() == 3);
    CHECK(lowered.instructions[0] == (0x4c98000000070000ULL | 251 | (15ULL << 39) | (u64{offset} << 20)));
    CHECK(lowered.instructions[1] == (0x5c18000000070000ULL | low_register | (u64{low_register} << 8) |
                                      (251ULL << 20) | (3ULL << 39) | (1ULL << 48)));
    const auto make_high = [](u32 destination, u32 low_reg, u32 high_reg, u32 word_address,
                              u32 guard, u32 carry) {
        return Instruction{{0xa11U | (guard << 12) | (destination << 16) | (low_reg << 24),
                            word_address << 8, 0x000f0c00 | (carry << 23) | high_reg, 0}};
    };
    const std::array high_programs{
        make_high(61, 51, 50, 0x67, 10, 2),
        make_high(47, 8, 13, 0x83, 4, 1),
        make_high(44, 70, 71, 0x70, 4, 5),
    };
    for (const auto high : high_programs) {
        const auto result = LowerToMaxwell(std::array{high, Exit});
        INFO(result.error);
        REQUIRE(result);
        const u64 predicate = u64{high.Bits(12, 4)} << 16;
        REQUIRE(result.instructions.size() == 7);
        CHECK(result.instructions[1] == (0x36f8000000000000ULL | predicate | 250 |
              (u64{high.Bits(24, 8)} << 8) | (u64{high.Bits(64, 8)} << 39) | (1ULL << 20)));
        CHECK(result.instructions[3] == (0x5ca0000000000000ULL | predicate | 252 |
              (252ULL << 8) | (255ULL << 20) | (u64{high.Bits(87, 3)} << 39)));
        CHECK(result.instructions[5] == (0x5c10000000000000ULL | predicate | high.Bits(16, 8) |
              (250ULL << 8) | (251ULL << 20)));
    }
}
