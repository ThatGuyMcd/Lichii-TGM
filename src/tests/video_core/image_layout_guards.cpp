// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>
#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/image_layout_guards.h"

TEST_CASE("Sparse image granularity must describe one complete format page",
          "[video_core][image_layout]") {
    using Vulkan::SparseBlockIsOnePage;
    CHECK(SparseBlockIsOnePage(128, 128, 1, 1, 4, 65536));
    CHECK(SparseBlockIsOnePage(256, 256, 4, 4, 16, 65536));
    CHECK_FALSE(SparseBlockIsOnePage(65549, 339860304, 1, 1, 4, 65536));
    CHECK_FALSE(SparseBlockIsOnePage(0, 128, 1, 1, 4, 65536));
    CHECK_FALSE(SparseBlockIsOnePage(2, 256, 4, 4, 16, 65536));
    CHECK_FALSE(SparseBlockIsOnePage(257, 256, 4, 4, 16, 65536));
    CHECK_FALSE(SparseBlockIsOnePage(256, 256, 4, 4, 16, 65537));
    CHECK_FALSE(SparseBlockIsOnePage(256, 256, 0, 4, 16, 65536));
    CHECK_FALSE(SparseBlockIsOnePage(256, 256, 4, 4, 0, 65536));
    CHECK_FALSE(SparseBlockIsOnePage((std::numeric_limits<u32>::max)(),
                                    (std::numeric_limits<u32>::max)(), 1, 1, 16, 65536));
}

TEST_CASE("Same image blits keep both source and destination in General layout",
          "[video_core][image_layout]") {
    CHECK(Vulkan::BlitDestinationLayout(true) == VK_IMAGE_LAYOUT_GENERAL);
    CHECK(Vulkan::BlitDestinationLayout(false) == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
}
