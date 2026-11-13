// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vulkan/vulkan.h>
#include "common/common_types.h"

namespace Vulkan {

constexpr bool SparseBlockIsOnePage(u32 width, u32 height, u32 block_width, u32 block_height,
                                    u64 block_bytes, u64 page_bytes) {
    if (block_width == 0 || block_height == 0 || block_bytes == 0 ||
        width < block_width || height < block_height || width % block_width != 0 ||
        height % block_height != 0 || page_bytes % block_bytes != 0) {
        return false;
    }
    // The product of two u32 dimensions fits in u64; divide the byte count first to avoid
    // overflow even when a broken driver reports enormous sparse blocks.
    return u64{width / block_width} * (height / block_height) == page_bytes / block_bytes;
}

constexpr VkImageLayout BlitDestinationLayout(bool same_image) {
    return same_image ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}

} // namespace Vulkan
