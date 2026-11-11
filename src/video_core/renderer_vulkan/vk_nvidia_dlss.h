// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// NVIDIA DLSS through Windows Streamline or native Linux NGX. Runtime libraries
// and optional neural model data are supplied separately.

#pragma once

#include <array>
#include <atomic>
#include <optional>
#include <memory>

#include "common/common_types.h"
#include "common/settings_enums.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

struct NVSDK_NGX_Handle;
struct NVSDK_NGX_Parameter;
namespace VideoCore { struct DlssStatus; }
namespace VideoCore::NeuralRendering { class Backend; }

namespace Vulkan {

class Device;
class MemoryAllocator;
class Scheduler;

class NvidiaDlssUpscaler {
public:
    struct Image {
        VkImageView view;
        VkImage image;
        VkFormat format;
        VkImageSubresourceRange range;     ///< The view's level and layer, with the view's aspect.
        VkImageAspectFlags barrier_aspect; ///< Every aspect of the image's format.
    };

    struct Inputs {
        Image color;          ///< Jittered scene colour, linear HDR or tonemapped.
        Image depth;          ///< Device depth (reversed) at the render size.
        Image motion_vectors; ///< Motion to the previous frame at the render size.
        /// R16F mask at the render size (GENERAL layout): where DLSS should trust this frame's
        /// colour over its history (things without motion vectors). Optional.
        std::optional<Image> bias_mask;
        VkExtent2D render_size;
        VkExtent2D upscale_size;
        /// This frame's sample offset in render pixels, as the game hands its own DLSS.
        std::array<f32, 2> jitter;
        /// Turns a motion vector into render pixels (the render size for UV offsets).
        std::array<f32, 2> motion_vector_scale;
        bool reset;
        /// Depth is reversed (1 near, 0 far); Ocarina of Time's is not.
        bool depth_inverted = true;
        /// -1: EDEN_NX2_DLSS_HDR decides; 0: colour is tonemapped LDR; 1: linear HDR.
        int color_hdr = -1;
        /// sl::DLSSPreset for this caller when EDEN_NX2_DLSS_PRESET is unset; -1: preset K.
        int preset = -1;
        /// The game's exposure for HDR colour (what its tonemap multiplies the colour by), given
        /// to DLSS in place of its auto exposure. Optional.
        std::optional<f32> exposure;
        /// The game's camera (OpenGL clip space, row-major, column vectors), for the constants
        /// frame generation reads; without it they are identities.
        struct Camera {
            /// Previous clip from current NDC (z = depth * 2 - 1).
            std::array<f32, 16> reproject;
            std::array<f32, 16> view_projection;
            /// The image's first row is NDC y = +1.
            bool flip_y;
        };
        std::optional<Camera> camera;
    };

    /// Throws vk::Exception when the platform runtime has no DLSS for this device.
    explicit NvidiaDlssUpscaler(const Device& device, Scheduler& scheduler,
                                    MemoryAllocator& memory_allocator);
    ~NvidiaDlssUpscaler();

    /// Records one evaluation into the output image (RGBA16F at the upscale size, GENERAL
    /// layout). Returns false when DLSS can't run, so the caller can fall back to FSR 3.1.
    bool Upscale(const Inputs& inputs);
    [[nodiscard]] static VideoCore::DlssStatus Status();

    [[nodiscard]] bool Failed() const noexcept {
        return failed.load();
    }

    /// True only once the latest queued evaluation recorded successfully.
    [[nodiscard]] bool EvaluationSucceeded() const noexcept {
        return successful_frame_index.load(std::memory_order_acquire) == frame_index &&
               !failed.load(std::memory_order_relaxed);
    }
    [[nodiscard]] VkImage OutputImage() const noexcept {
        return *output_image;
    }
    [[nodiscard]] VkImageView OutputView() const noexcept {
        return *output_view;
    }
    /// Nearest-neighbour clamp sampler (normalized coordinates), for reading the output.
    [[nodiscard]] VkSampler PointSampler() const noexcept {
        return *point_sampler;
    }

    [[nodiscard]] VkSampler LinearSampler() const noexcept { return *linear_sampler; }

private:
    void CreateSampler();
    void CreateOutput(VkExtent2D upscale_size);
    void CreateExposure();
    static void ClearExposure(vk::CommandBuffer cmdbuf, VkImage image, f32 value);

    const Device& device;
    Scheduler& scheduler;
    MemoryAllocator& memory_allocator;
#ifdef EGLANTINE_STREAMLINE
    void* set_options{}; ///< slDLSSSetOptions
    void* get_state{};   ///< slDLSSGetState
    void* get_optimal_settings{}; ///< slDLSSGetOptimalSettings
    u32 applied_preset_value{~0u}; ///< sl::DLSSPreset last given to DLSS
#endif
#ifdef EGLANTINE_NGX
    void PrepareNeuralRendering(const Inputs& inputs, bool requested);
    std::unique_ptr<VideoCore::NeuralRendering::Backend> neural_rendering;
    VkImageView neural_motion_view{};
    bool neural_requested{};
    bool neural_failed{};
    bool neural_model_verified{};
    NVSDK_NGX_Handle* feature{};
    NVSDK_NGX_Parameter* parameters{};
    bool ngx_depth_inverted = true;
    bool ngx_hdr_input{};
    bool ngx_auto_exposure{};
    u32 ngx_preset{~0u};
#endif
    Settings::NvidiaDlssQuality quality_mode{Settings::NvidiaDlssQuality::Automatic};
    VkExtent2D render_extent{};
    VkExtent2D upscale_extent{};
    u32 frame_index{};
    std::atomic<u32> successful_frame_index{~0u};
    vk::Image output_image;
    vk::ImageView output_view;
    vk::Image exposure_image; ///< 1x1 R32F, cleared to Inputs::exposure before each evaluation
    vk::ImageView exposure_view;
    bool applied_auto_exposure{};
    bool applied_hdr_input{};
    vk::Sampler point_sampler;
    vk::Sampler linear_sampler;
    std::atomic<bool> failed{};
    std::atomic<bool> vram_warned{};
};

} // namespace Vulkan
