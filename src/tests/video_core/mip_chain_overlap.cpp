// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/texture_cache/image_base.h"
#include "video_core/texture_cache/util.h"

namespace {
using namespace VideoCommon;

constexpr GPUVAddr GpuAddress = 0x430000;
constexpr DAddr DeviceAddress = 0x740000;

ImageInfo MakeImage(Extent3D size, s32 levels, Extent3D block, s32 layers = 1) {
    ImageInfo info;
    info.type = ImageType::e2D;
    info.format = PixelFormat::R16G16B16A16_FLOAT;
    info.size = size;
    info.resources = {.levels = levels, .layers = layers};
    info.block = block;
    info.layer_stride = CalculateLayerStride(info);
    info.maybe_unaligned_layer_stride = CalculateLayerSize(info);
    return info;
}

TEST_CASE("Single-layer mip chains can share a compatible level zero",
          "[video_core][texture_cache][mip_chain_overlap]") {
    const auto target = MakeImage({608, 336, 1}, 1, {0, 4, 0});
    const auto chain = MakeImage(target.size, 4, target.block);
    REQUIRE(chain.layer_stride != target.layer_stride);
    REQUIRE(chain.maybe_unaligned_layer_stride != target.maybe_unaligned_layer_stride);
    const ImageBase target_image{target, GpuAddress, DeviceAddress};
    const auto joined =
        ResolveOverlap(chain, GpuAddress, DeviceAddress, target_image, true, false, false);
    CHECK(joined.has_value());
    if (joined) {
        CHECK(joined->resources.levels == 4);
        CHECK(joined->resources.layers == 1);
    }

    const ImageBase chain_image{chain, GpuAddress, DeviceAddress};
    const auto view = FindSubresource(target, chain_image, GpuAddress, {}, false, false);
    CHECK(view.has_value());
    if (view) {
        CHECK(view->level == 0);
        CHECK(view->layer == 0);
    }

    // The stride exemption owes its validity to matching layout and the same base address.
    auto tiled = target;
    tiled.native_tiling = !chain.native_tiling;
    auto sparse = target;
    sparse.is_sparse = true;
    auto sampled = target;
    sampled.num_samples = 2;
    for (const auto& changed : {MakeImage(target.size, 1, {0, 3, 0}),
                                MakeImage(target.size, 1, target.block, 2),
                                MakeImage({target.size.width + 1, target.size.height, 1}, 1,
                                          target.block),
                                tiled, sparse, sampled}) {
        CHECK_FALSE(ResolveOverlap(changed, GpuAddress, DeviceAddress, chain_image, true, false,
                                   false));
        CHECK_FALSE(FindSubresource(changed, chain_image, GpuAddress, {}, false, false));
    }
    const u32 mip1 = chain_image.mip_level_offsets[1];
    REQUIRE(mip1 != 0);
    CHECK_FALSE(FindSubresource(target, chain_image, GpuAddress + mip1, {}, false, false));
}

TEST_CASE("An interior mip view does not replace its parent",
          "[video_core][texture_cache][mip_chain_overlap]") {
    const auto chain = MakeImage({64, 64, 1}, 2, {});
    const ImageBase chain_image{chain, GpuAddress, DeviceAddress};
    const u32 mip1 = chain_image.mip_level_offsets[1];
    REQUIRE(mip1 != 0);
    auto matching = MakeImage({32, 32, 1}, 1, {});
    matching.layer_stride = 0; // Render targets do not always specify a stride.
    const auto view = FindSubresource(matching, chain_image, GpuAddress + mip1, {}, false, false);
    REQUIRE(view);
    CHECK(view->level == 1);
    CHECK_FALSE(ResolveOverlap(matching, GpuAddress + mip1, DeviceAddress + mip1, chain_image,
                               true, false, false));

    auto wrong = MakeImage({128, 128, 1}, 1, {});
    wrong.layer_stride = 0;
    CHECK_FALSE(FindSubresource(wrong, chain_image, GpuAddress + mip1, {}, false, false));
    CHECK_FALSE(ResolveOverlap(wrong, GpuAddress + mip1, DeviceAddress + mip1, chain_image,
                               true, false, false));
}
} // Anonymous namespace
