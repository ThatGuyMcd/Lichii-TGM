// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/vulkan_common/unbacked_image_policy.h"

namespace {
using Vulkan::ShouldLeaveImageUnbacked;
constexpr u64 GiB = u64{1} << 30;
constexpr u64 Vram = 16 * GiB;

TEST_CASE("Images up to the texel threshold are always backed", "[video_core]") {
    REQUIRE_FALSE(ShouldLeaveImageUnbacked(u64{1} << 28, 100 * GiB, Vram));
    REQUIRE_FALSE(ShouldLeaveImageUnbacked(12288ULL * 12288, 100 * GiB, Vram));
}

TEST_CASE("A large depth map that fits in a quarter of VRAM is backed", "[video_core]") {
    // 18432x18432 D16: 339M texels, 648 MiB.
    constexpr u64 texels = 18432ULL * 18432;
    REQUIRE_FALSE(ShouldLeaveImageUnbacked(texels, texels * 2, Vram));
    // The same image on a card with 2 GiB of VRAM is too big.
    REQUIRE(ShouldLeaveImageUnbacked(texels, texels * 2, 2 * GiB));
}

TEST_CASE("A sparse texture array far beyond VRAM stays unbacked", "[video_core]") {
    constexpr u64 texels = 4500ULL * 1000 * 1000;
    REQUIRE(ShouldLeaveImageUnbacked(texels, 23 * GiB, Vram));
}

TEST_CASE("The limit is a quarter of the device-local memory", "[video_core]") {
    constexpr u64 texels = (u64{1} << 28) + 1;
    REQUIRE_FALSE(ShouldLeaveImageUnbacked(texels, Vram / 4, Vram));
    REQUIRE(ShouldLeaveImageUnbacked(texels, Vram / 4 + 1, Vram));
}

TEST_CASE("Unbacked images keep the device-dependent classification used when allocating them",
          "[video_core][unbacked_fallback]") {
    constexpr u64 texels = 16384ULL * 16384 * 2;
    // On an 8 GiB device this can be backed. If an allocation temporarily fails, the texture
    // cache must retry backing it on its first write rather than treating it as permanently
    // sparse merely because its requirement exceeds a fixed 1 GiB threshold.
    REQUIRE_FALSE(ShouldLeaveImageUnbacked(texels, GiB + GiB / 2, 8 * GiB));
    // The reverse matters on a small device: an image below 1 GiB can still be too large to
    // back, and must not trigger a replacement attempt on every write.
    REQUIRE(ShouldLeaveImageUnbacked(texels, 3 * GiB / 4, 2 * GiB));
    // Compressed light volumes stay backable even though their texel count is large.
    constexpr u64 volume_texels = 1408ULL * 256 * 1008;
    constexpr u64 bc6h_bytes = (1408ULL / 4) * (256 / 4) * 1008 * 16;
    REQUIRE_FALSE(ShouldLeaveImageUnbacked(volume_texels, bc6h_bytes, 2 * GiB));
}

TEST_CASE("An unknown memory requirement is treated as unbackable", "[video_core]") {
    REQUIRE(ShouldLeaveImageUnbacked((u64{1} << 28) + 1, 0, Vram));
}
} // namespace
