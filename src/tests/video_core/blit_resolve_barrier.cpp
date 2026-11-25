// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "video_core/vulkan_common/vulkan_wrapper.h"
#include "video_core/renderer_vulkan/image_layout_guards.h"
#include "video_core/texture_cache/types.h"

namespace {
using namespace Vulkan;
using VideoCommon::Region2D;

struct Fermi2D {
    enum class Filter { Nearest, Bilinear };
};

#include "blit_resolve_barrier.inc"

enum class Command { LegacyBarrier, Sync2Barrier, Blit, Resolve };

struct TransferTrace {
    std::array<Command, 3> commands{};
    size_t command_count{};
    VkImageMemoryBarrier2 post_barrier{};
    VkImage transfer_source{};
    VkImage transfer_destination{};
    VkImageLayout source_layout{};
    VkImageLayout destination_layout{};
    VkImageSubresourceLayers source_layers{};
    VkImageSubresourceLayers destination_layers{};
    u32 region_count{};

    void Record(Command command) {
        if (command_count < commands.size()) {
            commands[command_count] = command;
        }
        ++command_count;
    }
};

TransferTrace& Trace(VkCommandBuffer command) {
    return *reinterpret_cast<TransferTrace*>(command);
}

VKAPI_ATTR void VKAPI_CALL RecordLegacy(VkCommandBuffer command, VkPipelineStageFlags source,
                                       VkPipelineStageFlags destination, VkDependencyFlags,
                                       u32, const VkMemoryBarrier*, u32,
                                       const VkBufferMemoryBarrier*, u32 image_count,
                                       const VkImageMemoryBarrier* barriers) {
    auto& trace = Trace(command);
    trace.Record(Command::LegacyBarrier);
    if (image_count == 1) {
        const auto& barrier = barriers[0];
        trace.post_barrier.srcStageMask = source;
        trace.post_barrier.dstStageMask = destination;
        trace.post_barrier.srcAccessMask = barrier.srcAccessMask;
        trace.post_barrier.dstAccessMask = barrier.dstAccessMask;
        trace.post_barrier.oldLayout = barrier.oldLayout;
        trace.post_barrier.newLayout = barrier.newLayout;
        trace.post_barrier.image = barrier.image;
        trace.post_barrier.subresourceRange = barrier.subresourceRange;
    }
}

VKAPI_ATTR void VKAPI_CALL RecordSync2(VkCommandBuffer command, const VkDependencyInfo* info) {
    auto& trace = Trace(command);
    trace.Record(Command::Sync2Barrier);
    if (info->imageMemoryBarrierCount == 1) {
        trace.post_barrier = info->pImageMemoryBarriers[0];
    }
}

template <typename Region>
void RecordCopy(TransferTrace& trace, VkImage source, VkImageLayout source_layout,
                VkImage destination, VkImageLayout destination_layout, u32 region_count,
                const Region* regions) {
    trace.transfer_source = source;
    trace.transfer_destination = destination;
    trace.source_layout = source_layout;
    trace.destination_layout = destination_layout;
    trace.region_count = region_count;
    if (region_count == 1) {
        trace.source_layers = regions[0].srcSubresource;
        trace.destination_layers = regions[0].dstSubresource;
    }
}

VKAPI_ATTR void VKAPI_CALL RecordBlit(VkCommandBuffer command, VkImage source,
                                     VkImageLayout source_layout, VkImage destination,
                                     VkImageLayout destination_layout, u32 region_count,
                                     const VkImageBlit* regions, VkFilter) {
    auto& trace = Trace(command);
    trace.Record(Command::Blit);
    RecordCopy(trace, source, source_layout, destination, destination_layout, region_count, regions);
}

VKAPI_ATTR void VKAPI_CALL RecordResolve(VkCommandBuffer command, VkImage source,
                                        VkImageLayout source_layout, VkImage destination,
                                        VkImageLayout destination_layout, u32 region_count,
                                        const VkImageResolve* regions) {
    auto& trace = Trace(command);
    trace.Record(Command::Resolve);
    RecordCopy(trace, source, source_layout, destination, destination_layout, region_count, regions);
}
} // namespace

TEST_CASE("Native image blit and resolve post barriers cover later transfers",
          "[video_core][blit_resolve_barrier]") {
    const bool sync2 = GENERATE(false, true);
    enum class Transfer { Blit, SameImageBlit, Resolve };
    const auto transfer = GENERATE(Transfer::Blit, Transfer::SameImageBlit, Transfer::Resolve);
    CAPTURE(sync2, static_cast<int>(transfer));

    vk::DeviceDispatch dispatch{};
    dispatch.vkCmdPipelineBarrier = RecordLegacy;
    dispatch.vkCmdPipelineBarrier2 = sync2 ? RecordSync2 : nullptr;
    dispatch.vkCmdBlitImage = RecordBlit;
    dispatch.vkCmdResolveImage = RecordResolve;

    TransferTrace trace;
    const auto source = reinterpret_cast<VkImage>(&trace.transfer_source);
    const bool same_image = transfer == Transfer::SameImageBlit;
    const auto destination =
        same_image ? source : reinterpret_cast<VkImage>(&trace.transfer_destination);
    const bool resolve = transfer == Transfer::Resolve;
    const auto aspect = resolve ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
    const VkImageSubresourceLayers src_layers{aspect, 0, 1, 2};
    const VkImageSubresourceLayers dst_layers{aspect, resolve ? 0U : 2U, 2, 2};
    const Region2D src_region{{4, 8}, {20, 24}};
    const Region2D dst_region{{1, 3}, {17, 19}};
    RecordTransfer(vk::CommandBuffer{reinterpret_cast<VkCommandBuffer>(&trace), dispatch},
                   Fermi2D::Filter::Nearest, dst_region, src_region, destination, source,
                   dst_layers, src_layers, aspect, resolve);

    const Command barrier_command = sync2 ? Command::Sync2Barrier : Command::LegacyBarrier;
    const Command copy_command = resolve ? Command::Resolve : Command::Blit;
    const std::array expected_commands{barrier_command, copy_command, barrier_command};
    REQUIRE(trace.command_count == expected_commands.size());
    CHECK(trace.commands == expected_commands);
    CHECK(trace.transfer_source == source);
    CHECK(trace.transfer_destination == destination);
    CHECK(trace.region_count == 1);
    CHECK(trace.source_layers.mipLevel == src_layers.mipLevel);
    CHECK(trace.destination_layers.mipLevel == dst_layers.mipLevel);
    CHECK(trace.source_layout == VK_IMAGE_LAYOUT_GENERAL);
    const auto expected_layout = same_image ? VK_IMAGE_LAYOUT_GENERAL
                                            : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    CHECK(trace.destination_layout == expected_layout);

    const auto& barrier = trace.post_barrier;
    constexpr auto transfer_access = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    constexpr auto destination_stages = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT |
                                        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
                                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    CHECK((barrier.dstAccessMask & transfer_access) == transfer_access);
    CHECK((barrier.dstStageMask & destination_stages) == destination_stages);
    CHECK(barrier.srcStageMask == VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
    CHECK(barrier.srcAccessMask == VK_ACCESS_2_TRANSFER_WRITE_BIT);
    CHECK(barrier.oldLayout == expected_layout);
    CHECK(barrier.newLayout == VK_IMAGE_LAYOUT_GENERAL);
    CHECK(barrier.image == destination);
    CHECK(barrier.subresourceRange.aspectMask == aspect);
    CHECK(barrier.subresourceRange.baseMipLevel == 0);
    CHECK(barrier.subresourceRange.levelCount == VK_REMAINING_MIP_LEVELS);
    CHECK(barrier.subresourceRange.baseArrayLayer == 0);
    CHECK(barrier.subresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS);
}
