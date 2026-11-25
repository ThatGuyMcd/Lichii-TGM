// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include "video_core/native_indirect_reset.h"

TEST_CASE("Native indirect reset fingerprints the complete program bytes", "[native_reset]") {
    namespace Reset = VideoCommon::NativeIndirectReset;
    std::array<u8, Reset::ProgramByteCount> code{};
    for (size_t i = 0; i < code.size(); ++i) {
        code[i] = static_cast<u8>((17 + i * 73) ^ (i / 7));
    }
    constexpr Reset::Fingerprint expected{
        0xa1, 0xfc, 0xbc, 0x9b, 0x2b, 0x6c, 0xfb, 0x71, 0x05, 0xc6, 0xd2,
        0x1d, 0x88, 0xef, 0x94, 0xb5, 0xef, 0x00, 0xf6, 0x23, 0x77, 0x94,
        0xfa, 0x87, 0x75, 0xf0, 0x3d, 0xcc, 0x40, 0xcb, 0x28, 0x82,
    };
    REQUIRE(Reset::MatchesFingerprint(code, expected));
    REQUIRE_FALSE(Reset::MatchesFingerprint(std::span{code}.first(code.size() - 1), expected));
    REQUIRE_FALSE(Reset::MatchesFingerprint(
        std::array<u8, Reset::ProgramByteCount + 1>{}, expected));
    REQUIRE_FALSE(Reset::Matches(code));
    for (size_t i = 0; i < code.size(); ++i) {
        auto other = code;
        other[i] ^= 1;
        REQUIRE_FALSE(Reset::MatchesFingerprint(other, expected));
    }
    code[0] = 0x21;
    code[1] = 0x73;
    REQUIRE_FALSE(Reset::Matches(code));
    REQUIRE(Reset::Record == std::array<u32, 5>{0, 1, 0, 0, 0});
}

TEST_CASE("Native indirect reset list ranges never wrap or truncate", "[native_reset]") {
    namespace Reset = VideoCommon::NativeIndirectReset;
    constexpr u32 size = ~u32{0};
    const auto range = Reset::GetListRange(0x1000, size, 7, 18);
    REQUIRE(range);
    REQUIRE(range->address == 0x1038);
    REQUIRE(range->bytes == 144);
    REQUIRE_FALSE(Reset::GetListRange(0x1000, size, 0, 0));
    REQUIRE_FALSE(Reset::GetListRange(0x1000, size, 0, (1U << 20) + 1));
    REQUIRE_FALSE(Reset::GetListRange(~u64{0}, size, 0, 1));
    REQUIRE_FALSE(Reset::GetListRange(~u64{0} - 15, size, 1, 1));
    REQUIRE(Reset::GetListRange(~u64{0} - 16, size, 1, 1));
}

TEST_CASE("Native indirect reset stays inside its list descriptor", "[native_reset]") {
    namespace Reset = VideoCommon::NativeIndirectReset;
    REQUIRE_FALSE(Reset::GetListRange(0, 16, 0, 1));
    REQUIRE_FALSE(Reset::GetListRange(0x1000, 7, 0, 1));
    REQUIRE_FALSE(Reset::GetListRange(0x1000, 16, 2, 1));
    REQUIRE_FALSE(Reset::GetListRange(0x1000, 16, 1, 2));
    REQUIRE_FALSE(Reset::GetListRange(0x1000, 16, ~u32{0}, 1));
    const auto range = Reset::GetListRange(0x1000, 23, 1, 1);
    REQUIRE(range);
    REQUIRE(range->address == 0x1008);
    REQUIRE(range->bytes == 8);
}
