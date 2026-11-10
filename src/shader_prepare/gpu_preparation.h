// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string_view>
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"

namespace ShaderPreparation {
// The caller owns the title's preparation/runtime-cache locks for a real user cache.
Vulkan::NativePreparationResult PrepareGpuPipelines(const std::filesystem::path& directory,
    size_t workers, std::stop_token stop,
    const VideoCore::DiskResourceLoadCallback& progress = {},
    const std::function<void(std::string_view)>& device_ready = {});
} // namespace ShaderPreparation
