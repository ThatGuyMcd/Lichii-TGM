// SPDX-License-Identifier: GPL-3.0-or-later
#include "shader_prepare/gpu_preparation.h"
#include <memory>
#include <stdexcept>
#include "common/settings.h"
#include "common/scope_exit.h"
#include "core/device_memory.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/renderer_vulkan/blit_image.h"
#include "video_core/renderer_vulkan/vk_buffer_cache.h"
#include "video_core/renderer_vulkan/vk_descriptor_buffer.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_render_pass_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/shader_notify.h"
#include "video_core/oot_dlss.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_library.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#ifdef EGLANTINE_NVIDIA_DLSS
#include "video_core/vulkan_common/nvidia_dlss.h"
#endif


namespace ShaderPreparation {
namespace fs = std::filesystem;
Vulkan::NativePreparationResult PrepareGpuPipelines(const fs::path& directory, size_t workers,
                                          std::stop_token stop, const VideoCore::DiskResourceLoadCallback& progress,
                                          const std::function<void(std::string_view)>& device_ready) {
    using namespace Vulkan;
#ifdef EGLANTINE_NVIDIA_DLSS
    NvidiaDlss::Start();
    SCOPE_EXIT { NvidiaDlss::Stop(); };
#endif
    auto library = OpenLibrary();
    vk::InstanceDispatch dispatch;
    auto instance = CreateInstance(*library, dispatch, VK_API_VERSION_1_1);
    const auto physical = instance.EnumeratePhysicalDevices();
    const auto index = Settings::values.vulkan_device.GetValue();
    if (index >= physical.size()) throw std::runtime_error("The selected Vulkan GPU is unavailable.");
    Device device{*instance, vk::PhysicalDevice{physical[index], dispatch}, VK_NULL_HANDLE, dispatch};
    if (device_ready) device_ready(device.GetModelName());
    MemoryAllocator allocator{device};
    StateTracker tracker;
    Scheduler scheduler{device, tracker};
    StagingBufferPool staging{device, allocator, scheduler};
    DescriptorPool descriptors{device, scheduler};
    auto guest = std::make_unique<GuestDescriptorQueue>(device,
        UpdateDescriptorQueue::GUEST_FRAME_PAYLOAD_SIZE, device.IsExtDescriptorBufferSupported());
    auto compute = std::make_unique<ComputePassDescriptorQueue>(device,
        UpdateDescriptorQueue::COMPUTE_FRAME_PAYLOAD_SIZE);
    DescriptorBufferRing descriptor_buffer{device, allocator};
    BlitImageHelper blit{device, scheduler, tracker, descriptors};
    RenderPassCache render_passes{device};
    // Only virtual backing is reserved; shader preparation never maps guest game memory.
    Settings::values.memory_layout_mode.SetGlobal(true);
    Settings::values.memory_layout_mode.SetValue(Settings::MemoryLayout::Memory_4Gb);
    auto backing = std::make_unique<Core::DeviceMemory>();
    auto memory = std::make_unique<Tegra::MaxwellDeviceMemoryManager>(*backing);
    auto texture_runtime = std::make_unique<TextureCacheRuntime>(device, scheduler, allocator,
        staging, blit, render_passes, descriptors, *compute);
    auto textures = std::make_unique<TextureCache>(*texture_runtime, *memory);
    auto buffer_runtime = std::make_unique<BufferCacheRuntime>(device, allocator, scheduler,
        staging, *guest, *compute, descriptors);
    auto buffers = std::make_unique<BufferCache>(*memory, *buffer_runtime);
    VideoCore::ShaderNotify notify;
    NativePreparationResult result;
    {
        auto cache = std::make_unique<PipelineCache>(*memory, device, scheduler, descriptors,
            *guest, descriptor_buffer, render_passes, *buffers, *textures, notify, workers);
        const VideoCore::DiskResourceLoadCallback callback = progress ? progress :
            [](VideoCore::LoadCallbackStage, size_t, size_t) {};
        result = cache->PrepareGameShadersOnly(directory, stop, callback);
    }
    scheduler.Finish();
    return result;
}
} // namespace ShaderPreparation
