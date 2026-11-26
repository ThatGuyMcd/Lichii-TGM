// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <cstring>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "video_core/texture_cache/accelerated_swizzle.h"

namespace {
using VideoCommon::Accelerated::BlockLinearSwizzle3DParams;

#include "bcn_unswizzle_3d.inc"
} // namespace

TEST_CASE("Vulkan BCn 3D unswizzle uploads the guest layout at shader offset 56",
          "[video_core][bcn_unswizzle_3d]") {
    const auto native_tiling = GENERATE(0U, 1U);
    const auto block_bytes_log2 = GENERATE(3U, 4U);
    CAPTURE(native_tiling, block_bytes_log2);
    const BlockLinearSwizzle3DParams params{
        .origin{4, 8, 0},
        .destination{12, 16, 0},
        .bytes_per_block_log2 = block_bytes_log2,
        .slice_size = 8192,
        .block_size = 4096,
        .x_shift = 12,
        .block_height = 1,
        .block_height_mask = 1,
        .block_depth = 2,
        .block_depth_mask = 3,
        .native_tiling = native_tiling,
    };
    const auto pc = MakePushConstants(params, 8, 4, 12, 3);
    const auto* bytes = reinterpret_cast<const std::byte*>(&pc);
    u32 shader_native_tiling{};
    std::memcpy(&shader_native_tiling, bytes + 56, sizeof(shader_native_tiling));

    CHECK(shader_native_tiling == native_tiling);
    CHECK(sizeof(pc) == 80U);
    CHECK(pc.bytes_per_block_log2 == block_bytes_log2);
    CHECK(pc.block_depth_mask == 3U);
    CHECK(pc.origin[2] == 12U);
    CHECK(pc.blocks_dim[2] == 3U);
}
