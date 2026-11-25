// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
constexpr Instruction Exit{{0x794d, 0, 0x03800000, 0}};

constexpr Instruction HalfwordConvert(u32 destination, u32 source, u32 guard, bool sign,
                                      bool upper) {
    return {{0x306 | (guard << 12) | (destination << 16), source | (u32{upper} << 28),
             0x00101000 | (u32{sign} << 10), 0}};
}
}

TEST_CASE("Ampere signed halfword conversions lower without losing sign",
          "[video_core][ampere][tennis]") {
    for (const Instruction conversion :
         std::array{HalfwordConvert(17, 17, 7, true, false),
                    HalfwordConvert(28, 31, 7, true, false)}) {
        const auto lowered = Shader::Ampere::LowerToMaxwell(std::array{conversion, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        REQUIRE(lowered.instructions.size() == 2);
        REQUIRE(lowered.instructions.front() ==
                (0x5cb8000000072600ULL | conversion.Bits(16, 8) |
                 (u64{conversion.Bits(32, 8)} << 20)));
    }
}

TEST_CASE("Ampere halfword conversion keeps both selectors signedness and predicates",
          "[video_core][ampere][tennis]") {
    for (const u32 sign : {0U, 1U}) {
        for (const u32 half : {0U, 1U}) {
            for (const u32 guard : {0U, 7U, 9U}) {
                const auto native = HalfwordConvert(2, 3, guard, sign != 0, half != 0);
                const auto lowered = Shader::Ampere::LowerToMaxwell(std::array{native, Exit});
                REQUIRE(lowered);
                const u64 word = lowered.instructions.front();
                REQUIRE((word & 255) == 2);
                REQUIRE(((word >> 8) & 3) == 2);
                REQUIRE(((word >> 10) & 3) == 1);
                REQUIRE(((word >> 13) & 1) == sign);
                REQUIRE(((word >> 16) & 15) == guard);
                REQUIRE(((word >> 20) & 255) == 3);
                REQUIRE(((word >> 41) & 3) == half * 2);
            }
        }
    }
}
