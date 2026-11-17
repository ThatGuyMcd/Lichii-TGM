// SPDX-FileCopyrightText: Copyright (c) 2010 The WebM project authors. All Rights Reserved.
// SPDX-License-Identifier: GPL-3.0-or-later AND BSD-3-Clause
// ReferenceWriter adapts vpx_dsp/bitwriter.h::vpx_write from webmproject/libvpx
// commit 7e000a41a7727d5b9477dbbe5727431560d6a16c, through Lichii PR #2070.
// Upstream source: https://github.com/webmproject/libvpx/blob/7e000a41a7727d5b9477dbbe5727431560d6a16c/vpx_dsp/bitwriter.h
// The original BSD notice and additional patent grant are retained in
// licenses/libvpx-LICENSE and licenses/libvpx-PATENTS. Test inputs are generated here.

#include <bit>
#include <random>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "video_core/host1x/codecs/vp9.h"

namespace {

// libvpx vpx_dsp/bitwriter.h vpx_write
struct ReferenceWriter {
    std::vector<u8> buffer;
    u32 low_value{};
    u32 range{255};
    s32 count{-24};
    int carries{};

    void Write(bool bit, s32 probability) {
        const u32 split = 1 + (((range - 1) * static_cast<u32>(probability)) >> 8);
        u32 local_range = split;
        if (bit) {
            low_value += split;
            local_range = range - split;
        }
        s32 shift = std::countl_zero(local_range) - 24;
        local_range <<= shift;
        count += shift;
        if (count >= 0) {
            const s32 offset = shift - count;
            if ((low_value << (offset - 1)) & 0x80000000) {
                ++carries;
                std::size_t index = buffer.size() - 1;
                while (buffer[index] == 0xff) {
                    buffer[index--] = 0;
                }
                ++buffer[index];
            }
            buffer.push_back(static_cast<u8>(low_value >> (24 - offset)));
            low_value <<= offset;
            shift = count;
            low_value &= 0xffffff;
            count -= 8;
        }
        low_value <<= shift;
        range = local_range;
    }
};

} // namespace

TEST_CASE("VP9 range encoder propagates carries like libvpx", "[video_core][vp9]") {
    std::mt19937 rng(757);
    int carries = 0;
    for (int run = 0; run < 2000; ++run) {
        Tegra::Decoders::VpxRangeEncoder encoder;
        ReferenceWriter reference;
        reference.Write(false, 128);
        for (int i = 0; i < 2000; ++i) {
            const s32 probability = static_cast<s32>(1 + rng() % 255);
            const bool bit = rng() % 256 >= static_cast<u32>(probability);
            encoder.Write(bit, probability);
            reference.Write(bit, probability);
        }
        encoder.End();
        for (int i = 0; i < 32; ++i) {
            reference.Write(false, 128);
        }
        REQUIRE(encoder.GetBuffer() == reference.buffer);
        carries += reference.carries;
    }
    REQUIRE(carries > 0);
}
