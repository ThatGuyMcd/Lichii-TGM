// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace {
using namespace Vulkan;

struct BarrierCall {
    u32 legacy_calls{};
    u32 sync2_calls{};
    VkImageMemoryBarrier2 image{};
};

VKAPI_ATTR void VKAPI_CALL RecordLegacy(VkCommandBuffer command, VkPipelineStageFlags source,
                                       VkPipelineStageFlags destination, VkDependencyFlags,
                                       u32, const VkMemoryBarrier*, u32,
                                       const VkBufferMemoryBarrier*, u32 image_count,
                                       const VkImageMemoryBarrier* barriers) {
    auto& call = *reinterpret_cast<BarrierCall*>(command);
    ++call.legacy_calls;
    if (image_count == 1) {
        const auto& barrier = barriers[0];
        call.image.srcStageMask = source;
        call.image.dstStageMask = destination;
        call.image.srcAccessMask = barrier.srcAccessMask;
        call.image.dstAccessMask = barrier.dstAccessMask;
        call.image.oldLayout = barrier.oldLayout;
        call.image.newLayout = barrier.newLayout;
        call.image.subresourceRange = barrier.subresourceRange;
    }
}

VKAPI_ATTR void VKAPI_CALL RecordSync2(VkCommandBuffer command, const VkDependencyInfo* info) {
    auto& call = *reinterpret_cast<BarrierCall*>(command);
    ++call.sync2_calls;
    if (info->imageMemoryBarrierCount == 1) {
        call.image = info->pImageMemoryBarriers[0];
    }
}

struct DestinationRange {
    VkImageSubresourceRange SubresourceRange(VkImageAspectFlags aspect) const {
        return {aspect, 2, 1, 3, 2};
    }
};

void RecordProductionBarrier(vk::CommandBuffer cmdbuf) {
    const DestinationRange dst_range;
    const VkImage dst_image = VK_NULL_HANDLE;
    constexpr VkImageAspectFlags dst_aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
#include "reinterpret_barrier.inc"
}
} // namespace

TEST_CASE("Reinterpreted image post barrier makes transfer accesses available",
          "[video_core][reinterpret_barrier]") {
    for (const bool synchronization2 : {false, true}) {
        CAPTURE(synchronization2);
        vk::DeviceDispatch dispatch{};
        dispatch.vkCmdPipelineBarrier = RecordLegacy;
        if (synchronization2) {
            dispatch.vkCmdPipelineBarrier2 = RecordSync2;
        }
        BarrierCall call;
        RecordProductionBarrier(
            vk::CommandBuffer{reinterpret_cast<VkCommandBuffer>(&call), dispatch});

        REQUIRE(call.legacy_calls == (synchronization2 ? 0U : 1U));
        REQUIRE(call.sync2_calls == (synchronization2 ? 1U : 0U));
        const auto& barrier = call.image;
        constexpr auto transfer_access = VK_ACCESS_2_TRANSFER_READ_BIT |
                                         VK_ACCESS_2_TRANSFER_WRITE_BIT;
        constexpr auto destination_stages = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT |
                                            VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
                                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        CHECK((barrier.dstAccessMask & transfer_access) == transfer_access);
        CHECK((barrier.dstStageMask & destination_stages) == destination_stages);
        CHECK(barrier.srcStageMask == VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
        CHECK(barrier.srcAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
        CHECK(barrier.oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        CHECK(barrier.newLayout == VK_IMAGE_LAYOUT_GENERAL);
        CHECK(barrier.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
        CHECK(barrier.subresourceRange.baseMipLevel == 2U);
        CHECK(barrier.subresourceRange.layerCount == 2U);
    }
}
