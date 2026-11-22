// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <vector>

#include <boost/container/small_vector.hpp>
#include <catch2/catch_test_macros.hpp>

// Before the Vulkan headers: Xlib defines Success, which NvResult (nvdata.h) declares.
#include "video_core/renderer_vulkan/vk_state_tracker.h"

#include "common/common_types.h"
#include "common/div_ceil.h"
#include "common/scope_exit.h"
#include "video_core/renderer_vulkan/vk_compute_pass.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/texture_cache/image_info.h"
#include "video_core/texture_cache/util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_library.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace {

// Explicit GPU lane only. Missing Vulkan is a failure, never a silently skipped gate.
struct TestDevice {
    TestDevice() {
        library = Vulkan::OpenLibrary();
        if (!library) {
            FAIL("No Vulkan library");
        }
        try {
            instance.emplace(Vulkan::CreateInstance(*library, dld, VK_API_VERSION_1_1));
        } catch (const Vulkan::vk::Exception&) {
            FAIL("No Vulkan instance");
        }
        for (const VkPhysicalDevice physical : instance->EnumeratePhysicalDevices()) {
            try {
                device.emplace(**instance, Vulkan::vk::PhysicalDevice(physical, dld),
                               VK_NULL_HANDLE, dld);
                break;
            } catch (const Vulkan::vk::Exception&) {
                continue;
            }
        }
        if (!device) {
            FAIL("No Vulkan device");
        }
        UNSCOPED_INFO(device->GetDriverName() << " " << device->GetModelName());
    }

    std::shared_ptr<Common::DynamicLibrary> library;
    Vulkan::vk::InstanceDispatch dld;
    std::optional<Vulkan::vk::Instance> instance;
    std::optional<Vulkan::Device> device;
};

struct Transcoder {
    explicit Transcoder(const Vulkan::Device& device_)
        : device{device_}, allocator{device}, scheduler{device, state_tracker},
          staging{device, allocator, scheduler}, descriptor_pool{device, scheduler},
          queue{device, Vulkan::UpdateDescriptorQueue::COMPUTE_FRAME_PAYLOAD_SIZE},
          decoder{device, scheduler, descriptor_pool, staging, queue, allocator},
          encoder{device, scheduler, descriptor_pool, queue} {}

    // The blocks TextureCacheRuntime::TranscodeAstcToBcn uploads, decoded `batch` layers at a
    // time. Bytes the encoder does not write keep `fill`.
    std::vector<u8> Run(const VideoCommon::ImageInfo& info, std::span<const u8> guest, u32 batch,
                        bool bc3, u8 fill, u32 host_levels = 0) {
        const u32 width = info.size.width;
        const u32 height = info.size.height;
        const u32 levels = host_levels ? host_levels : static_cast<u32>(info.resources.levels);
        const u32 layers = static_cast<u32>(info.resources.layers);
        boost::container::small_vector<u64, 16> level_offsets;
        boost::container::small_vector<u64, 16> layer_bytes;
        u64 total = 0;
        for (u32 level = 0; level < levels; ++level) {
            const u32 blocks_x = Common::DivCeil((std::max)(width >> level, 1U), 4U);
            const u32 blocks_y = Common::DivCeil((std::max)(height >> level, 1U), 4U);
            layer_bytes.push_back(u64{blocks_x} * blocks_y * (bc3 ? 16 : 8));
            level_offsets.push_back(total);
            total += layer_bytes.back() * layers;
        }

        // Own host-visible allocations so flushing/invalidation is explicit even on
        // non-coherent memory types; HOST_COHERENT is only preferred by the allocator.
        const auto make_host_buffer = [&](u64 size, Vulkan::MemoryUsage usage) {
            return allocator.CreateBuffer(VkBufferCreateInfo{
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = size,
                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            }, usage);
        };
        auto input = make_host_buffer(guest.size(), Vulkan::MemoryUsage::Upload);
        REQUIRE(input.Mapped().size() >= guest.size());
        std::memcpy(input.Mapped().data(), guest.data(), guest.size());
        input.Flush();
        const Vulkan::StagingBufferRef map{
            .buffer = *input,
            .device_address = 0,
            .offset = 0,
            .mapped_span = input.Mapped(),
            .usage = Vulkan::MemoryUsage::Upload,
            .log2_level = 0,
            .index = 0,
        };
        auto download = make_host_buffer(total, Vulkan::MemoryUsage::Download);
        REQUIRE(download.Mapped().size() >= total);
        auto output = staging.Request(total, Vulkan::MemoryUsage::DeviceLocal, true);
        scheduler.Record([buffer = output.buffer, offset = output.offset, total, fill]
                         (Vulkan::vk::CommandBuffer cmdbuf) {
            cmdbuf.FillBuffer(buffer, offset, total, u32{fill} * 0x01010101U);
            const VkMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            };
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, barrier);
        });

        const Vulkan::vk::Image decode = allocator.CreateImage(VkImageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = {width, height, 1},
            .mipLevels = levels,
            .arrayLayers = batch,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        });
        std::vector<Vulkan::vk::ImageView> views;
        for (u32 level = 0; level < levels; ++level) {
            views.push_back(device.GetLogical().CreateImageView(VkImageViewCreateInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .image = *decode,
                .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .components{},
                .subresourceRange{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = level,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            }));
        }

        const auto swizzles = VideoCommon::FullUploadSwizzles(info);
        Vulkan::TranscodeAstcBatches(decoder, encoder, info, guest.size(), map, swizzles, *decode,
                                     views, batch, bc3, output.buffer, output.offset, total,
                                     level_offsets, layer_bytes);
        // The final decode submitted, while its encoder and copy are still pending.
        // A same-size request must not reclaim the held output at that boundary.
        auto pressure = staging.Request(total, Vulkan::MemoryUsage::DeviceLocal, true);
        REQUIRE((pressure.buffer != output.buffer || pressure.offset != output.offset));
        scheduler.Record([output_buffer = output.buffer, output_offset = output.offset,
                          pressure_buffer = pressure.buffer, pressure_offset = pressure.offset,
                          download_buffer = *download, total](Vulkan::vk::CommandBuffer cmdbuf) {
            cmdbuf.FillBuffer(pressure_buffer, pressure_offset, total, 0xA5A5A5A5U);
            const VkMemoryBarrier encoded{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            };
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, encoded);
            const VkBufferCopy copy{output_offset, 0, total};
            cmdbuf.CopyBuffer(output_buffer, download_buffer, copy);
            const VkMemoryBarrier copied{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            };
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_HOST_BIT, 0, copied);
        });
        // Stamp both held buffers with their final-use tick, just as the runtime does.
        staging.FreeDeferred(output);
        staging.FreeDeferred(pressure);
        scheduler.Finish();
        download.Invalidate();
        std::vector<u8> result(download.Mapped().begin(), download.Mapped().begin() + total);
        return result;
    }

    const Vulkan::Device& device;
    Vulkan::MemoryAllocator allocator;
    Vulkan::StateTracker state_tracker;
    Vulkan::Scheduler scheduler;
    Vulkan::StagingBufferPool staging;
    Vulkan::DescriptorPool descriptor_pool;
    Vulkan::ComputePassDescriptorQueue queue;
    Vulkan::ASTCDecoderPass decoder;
    Vulkan::BcEncoderPass encoder;
};

// Block-linear ASTC 4x4 data: void-extent blocks of random colours mixed with random blocks, so
// the decoder takes both its constant and its error paths and the encoder sees varied texels.
std::vector<u8> GuestAstc(size_t size) {
    std::mt19937 rng{2365};
    std::vector<u8> data(size);
    for (size_t block = 0; block + 16 <= size; block += 16) {
        for (size_t i = 0; i < 16; ++i) {
            data[block + i] = static_cast<u8>(rng());
        }
        if (rng() % 2 == 0) {
            constexpr u8 void_extent[8]{0xFC, 0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            std::memcpy(&data[block], void_extent, sizeof(void_extent));
        }
    }
    return data;
}

constexpr std::array<std::array<u8, 4>, 5> Colors{{
    {255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255},
    {0, 0, 0, 255}, {255, 255, 255, 255},
}};

// Every block within a layer/mip has its own constant colour. Block-linear addressing
// cannot change that colour; using different colours catches layer and mip offset errors.
std::vector<u8> ConstantGuestAstc(const VideoCommon::ImageInfo& info) {
    std::vector<u8> data(VideoCommon::CalculateGuestSizeInBytes(info));
    const auto swizzles = VideoCommon::FullUploadSwizzles(info);
    for (u32 layer = 0; layer < static_cast<u32>(info.resources.layers); ++layer) {
        for (size_t level = 0; level < swizzles.size(); ++level) {
            const size_t start = layer * size_t{info.layer_stride} + swizzles[level].buffer_offset;
            const size_t end = (std::min)(data.size(), layer * size_t{info.layer_stride} +
                (level + 1 < swizzles.size() ? swizzles[level + 1].buffer_offset : info.layer_stride));
            const auto& color = Colors[(layer + level) % Colors.size()];
            for (size_t block = start; block + 16 <= end; block += 16) {
                constexpr u8 header[]{0xFC, 0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                std::copy(std::begin(header), std::end(header), data.begin() + block);
                for (size_t channel = 0; channel < 4; ++channel) {
                    data[block + 8 + channel * 2] = color[channel];
                    data[block + 9 + channel * 2] = color[channel];
                }
            }
        }
    }
    return data;
}

// Independent BC1/BC3 endpoint/index decoding, not the production encoder or ASTC decoder.
std::array<u8, 4> DecodeBcPixel(const u8* block, bool bc3, u32 pixel) {
    const u8* rgb = block + (bc3 ? 8 : 0);
    const u32 first = rgb[0] | (u32{rgb[1]} << 8);
    const u32 second = rgb[2] | (u32{rgb[3]} << 8);
    const auto color565 = [](u32 color) -> std::array<u8, 4> {
        const u32 r = color >> 11, g = (color >> 5) & 63, b = color & 31;
        return {u8((r << 3) | (r >> 2)), u8((g << 2) | (g >> 4)),
                u8((b << 3) | (b >> 2)), 255};
    };
    std::array<std::array<u8, 4>, 4> palette{color565(first), color565(second)};
    for (size_t channel = 0; channel < 3; ++channel) {
        if (first > second || bc3) {
            palette[2][channel] = u8((2 * palette[0][channel] + palette[1][channel]) / 3);
            palette[3][channel] = u8((palette[0][channel] + 2 * palette[1][channel]) / 3);
        } else {
            palette[2][channel] = u8((palette[0][channel] + palette[1][channel]) / 2);
        }
    }
    palette[2][3] = 255;
    palette[3][3] = (first > second || bc3) ? 255 : 0;
    const u32 indices = rgb[4] | (u32{rgb[5]} << 8) | (u32{rgb[6]} << 16) | (u32{rgb[7]} << 24);
    auto result = palette[(indices >> (2 * pixel)) & 3];
    if (bc3) {
        std::array<u8, 8> alpha{block[0], block[1]};
        if (alpha[0] > alpha[1]) {
            for (u32 i = 1; i <= 6; ++i) alpha[i + 1] = u8(((7 - i) * alpha[0] + i * alpha[1]) / 7);
        } else {
            for (u32 i = 1; i <= 4; ++i) alpha[i + 1] = u8(((5 - i) * alpha[0] + i * alpha[1]) / 5);
            alpha[7] = 255;
        }
        u64 indices_alpha{};
        for (u32 i = 0; i < 6; ++i) indices_alpha |= u64{block[2 + i]} << (8 * i);
        result[3] = alpha[(indices_alpha >> (3 * pixel)) & 7];
    }
    return result;
}

void CheckConstantColors(std::span<const u8> output, const VideoCommon::ImageInfo& info,
                         u32 levels, bool bc3) {
    size_t offset{};
    const size_t block_size = bc3 ? 16 : 8;
    for (u32 level = 0; level < levels; ++level) {
        const u32 blocks_x = Common::DivCeil((std::max)(info.size.width >> level, 1U), 4U);
        const u32 blocks_y = Common::DivCeil((std::max)(info.size.height >> level, 1U), 4U);
        for (u32 layer = 0; layer < static_cast<u32>(info.resources.layers); ++layer) {
            INFO("mip " << level << " layer " << layer << " BC3 " << bc3);
            for (u32 block = 0; block < blocks_x * blocks_y; ++block) {
                REQUIRE(offset + block_size <= output.size());
                for (u32 pixel = 0; pixel < 16; ++pixel) {
                    REQUIRE(DecodeBcPixel(output.data() + offset, bc3, pixel) ==
                            Colors[(layer + level) % Colors.size()]);
                }
                offset += block_size;
            }
        }
    }
    REQUIRE(offset == output.size());
}

} // Anonymous namespace

// Adapted from Lichii #2393, aa59fa46a5ab13d48236850b5f9916fe224bd87f (zortos293).
// Local additions: explicit non-coherent visibility, held output pressure and independent colours.
// #2365: an array larger than the free pool decodes a few layers at a time on every GPU. The
// encoded blocks must not depend on how many layers each batch takes.
TEST_CASE("ASTC to BCn: a batched decode encodes the same bytes as the whole array",
          "[.][astc_bcn_batches]") {
    TestDevice test;
    Transcoder transcoder(*test.device);

    VideoCommon::ImageInfo info;
    info.format = VideoCore::Surface::PixelFormat::ASTC_2D_4X4_UNORM;
    info.type = VideoCommon::ImageType::e2D;
    info.size = {70, 38, 1};
    info.block = {0, 4, 0};
    info.resources.levels = 7;
    info.resources.layers = 5;
    info.layer_stride = VideoCommon::CalculateLayerStride(info);
    info.maybe_unaligned_layer_stride = VideoCommon::CalculateLayerSize(info);
    const std::vector<u8> guest = GuestAstc(VideoCommon::CalculateGuestSizeInBytes(info));

    for (const bool bc3 : {false, true}) {
        INFO("bc3 " << bc3);
        const std::vector<u8> whole = transcoder.Run(info, guest, 5, bc3, 0xCD);
        // Different fills: a byte the encoder skipped differs between the two runs.
        REQUIRE(transcoder.Run(info, guest, 5, bc3, 0x5A) == whole);
        for (const u32 batch : {1U, 2U, 3U}) {
            INFO("batch " << batch);
            CHECK(transcoder.Run(info, guest, batch, bc3, 0x5A) == whole);
        }
    }
}

TEST_CASE("ASTC batches preserve independent layer and mip colours with trimmed host mips",
          "[.][astc_bcn_batches]") {
    TestDevice test;
    Transcoder transcoder(*test.device);
    VideoCommon::ImageInfo info;
    info.format = VideoCore::Surface::PixelFormat::ASTC_2D_4X4_UNORM;
    info.type = VideoCommon::ImageType::e2D;
    info.size = {70, 38, 1};
    info.block = {0, 4, 0};
    info.resources.levels = 7;
    info.resources.layers = 5;
    info.layer_stride = VideoCommon::CalculateLayerStride(info);
    info.maybe_unaligned_layer_stride = VideoCommon::CalculateLayerSize(info);
    const auto guest = ConstantGuestAstc(info);
    for (bool bc3 : {false, true}) {
        for (u32 levels : {5U, 7U}) {
            for (u32 batch : {1U, 2U, 3U, 5U}) {
                INFO("batch " << batch << " host mips " << levels);
                const auto output = transcoder.Run(info, guest, batch, bc3, 0xCD, levels);
                CheckConstantColors(output, info, levels, bc3);
            }
        }
    }
}
