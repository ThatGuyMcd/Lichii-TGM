// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <fmt/format.h>
#include "common/logging.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "video_core/nvidia_dlss_policy.h"
#include "video_core/dlss_mode.h"
#include "video_core/renderer_base.h"
#include "video_core/oot_dlss.h"
#include "video_core/neural_rendering/backend.h"
#include "video_core/neural_rendering/model.h"
#include "video_core/neural_rendering/runtime.h"
#include "video_core/renderer_vulkan/vk_nvidia_dlss.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/ngx_api.h"
#include "video_core/vulkan_common/ngx_dlss.h"
#include "video_core/vulkan_common/nvidia_dlss.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {
namespace NR = VideoCore::NeuralRendering;
namespace {

std::atomic<u64> evaluations{};
std::atomic<u32> last_evaluation{};

/// Read an integer environment override, using the supplied fallback when the variable is
/// absent.
int EnvInt(const char *name, int fallback) {
    const char *value = std::getenv(name);
    return value ? std::atoi(value) : fallback;
}

/// Choose DLAA or a DLSS quality mode from the render-to-output width ratio.
NVSDK_NGX_PerfQuality_Value ModeFor(Settings::NvidiaDlssQuality mode) {
    switch (mode) {
    case Settings::NvidiaDlssQuality::Native: return NVSDK_NGX_PerfQuality_Value_DLAA;
    case Settings::NvidiaDlssQuality::Quality: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case Settings::NvidiaDlssQuality::Balanced: return NVSDK_NGX_PerfQuality_Value_Balanced;
    case Settings::NvidiaDlssQuality::Performance: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case Settings::NvidiaDlssQuality::UltraPerformance:
        return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    case Settings::NvidiaDlssQuality::Automatic: break;
    }
    return NVSDK_NGX_PerfQuality_Value_MaxQuality;
}

std::optional<VideoCommon::DlssMode::Range> QueryRange(
    NVSDK_NGX_Parameter& caps, Settings::NvidiaDlssQuality mode, VkExtent2D output) {
    void* callback{};
    if (NVSDK_NGX_FAILED(caps.Get(NVSDK_NGX_Parameter_DLSSOptimalSettingsCallback, &callback)) ||
        !callback) {
        return std::nullopt;
    }
    caps.Set(NVSDK_NGX_Parameter_Width, output.width);
    caps.Set(NVSDK_NGX_Parameter_Height, output.height);
    caps.Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(ModeFor(mode)));
    caps.Set(NVSDK_NGX_Parameter_RTXValue, 0);
    const auto query = reinterpret_cast<PFN_NVSDK_NGX_DLSS_GetOptimalSettingsCallback>(callback);
    if (NVSDK_NGX_FAILED(query(&caps))) {
        return std::nullopt;
    }
    u32 width{}, height{};
    if (NVSDK_NGX_FAILED(caps.Get(NVSDK_NGX_Parameter_OutWidth, &width)) ||
        NVSDK_NGX_FAILED(caps.Get(NVSDK_NGX_Parameter_OutHeight, &height))) {
        return std::nullopt;
    }
    VideoCommon::DlssMode::Range range{{width, height}, {width, height}};
    caps.Get(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width, &range.min.width);
    caps.Get(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height, &range.min.height);
    caps.Get(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width, &range.max.width);
    caps.Get(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height, &range.max.height);
    return range;
}

NVSDK_NGX_Resource_VK Resource(const NvidiaDlssUpscaler::Image &image, VkExtent2D size,
                               bool write = false) {
    NVSDK_NGX_Resource_VK resource{};
    resource.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    resource.Resource.ImageViewInfo = {image.view,   image.image, image.range,
                                       image.format, size.width,  size.height};
    resource.ReadWrite = write;
    return resource;
}

VkImageMemoryBarrier Barrier(const NvidiaDlssUpscaler::Image &image, VkAccessFlags src,
                             VkAccessFlags dst, VkImageLayout before, VkImageLayout after) {
    auto range = image.range;
    range.aspectMask = image.barrier_aspect;
    return {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = src,
            .dstAccessMask = dst,
            .oldLayout = before,
            .newLayout = after,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image.image,
            .subresourceRange = range};
}

} // namespace

/// Initialize the native NGX upscaler; NGX must already be available.
NvidiaDlssUpscaler::NvidiaDlssUpscaler(const Device &device_, Scheduler &scheduler_,
                                       MemoryAllocator &memory_allocator_)
    : device{device_}, scheduler{scheduler_}, memory_allocator{memory_allocator_} {
    evaluations.store(0, std::memory_order_release);
    if (!NvidiaDlss::IsAvailable()) {
        throw vk::Exception(VK_ERROR_FEATURE_NOT_PRESENT);
    }
    CreateSampler();
}

/// Serialize device-idle waits and release neural/NGX resources after renderer recording has
/// stopped.
NvidiaDlssUpscaler::~NvidiaDlssUpscaler() {
    evaluations.store(0, std::memory_order_release);
    // Renderer shutdown has already stopped recording; do not submit to released game channels.
    std::scoped_lock queue_lock{scheduler.submit_mutex};
    void(device.GetLogical().WaitIdle());
    neural_rendering.reset();
    const auto &api = Ngx::GetApi();
    if (feature)
        api.release(feature);
    if (parameters)
        api.destroy_parameters(parameters);
}

/// Record native DLSS and optional neural rendering, recreating changed resources and reporting
/// fallback on failure.
bool NvidiaDlssUpscaler::Upscale(const Inputs &in) {
    if (failed) {
        return false;
    }
    const bool hdr = VideoCommon::NvidiaDlssHdr(in.color_hdr,
                                                EnvInt("EDEN_NX2_DLSS_HDR", 1) != 0);
    const bool auto_exposure = hdr && !in.exposure &&
                               EnvInt("EDEN_NX2_DLSS_AUTO_EXPOSURE", 1) != 0;
    const char* preset_env = std::getenv("EDEN_NX2_DLSS_PRESET");
    const auto neural_options = Settings::GetNvidiaDlssRuntimeSettings();
    const u32 preset = VideoCommon::NvidiaDlssPreset(
        neural_options.legacy_preset, in.preset,
        preset_env ? std::optional{std::string_view{preset_env}} : std::nullopt,
        neural_options.preset);
    const bool extent_changed = !output_image || in.render_size.width != render_extent.width ||
                                in.render_size.height != render_extent.height ||
                                in.upscale_size.width != upscale_extent.width ||
                                in.upscale_size.height != upscale_extent.height;
    const bool options_changed =
        extent_changed || hdr != ngx_hdr_input || in.depth_inverted != ngx_depth_inverted ||
        auto_exposure != ngx_auto_exposure || preset != ngx_preset ||
        neural_options.quality != quality_mode;
    if (options_changed && output_image) {
        scheduler.Finish();
        if (feature) {
            Ngx::GetApi().release(feature);
            feature = nullptr;
        }
    }
    if (extent_changed) {
        if (neural_rendering) {
            scheduler.Finish();
            std::scoped_lock queue_lock{scheduler.submit_mutex};
            neural_rendering.reset();
        }
        CreateOutput(in.upscale_size);
        render_extent = in.render_size;
        upscale_extent = in.upscale_size;
    }
    ngx_hdr_input = hdr;
    ngx_depth_inverted = in.depth_inverted;
    ngx_auto_exposure = auto_exposure;
    ngx_preset = preset;
    quality_mode = neural_options.quality;
    if (in.exposure && !exposure_image) {
        CreateExposure();
    }
    const auto neural_mode = neural_options.neural_rendering;
    const bool requested = device.SupportsNeuralRendering() &&
                           neural_mode != Settings::NeuralRendering::Off;
    const auto neural_style = requested ? static_cast<NR::Style>(static_cast<u32>(neural_mode) - 1)
                                       : NR::Style::Standard;
    PrepareNeuralRendering(in, requested);
    const Image out{*output_view,
                    *output_image,
                    VK_FORMAT_R16G16B16A16_SFLOAT,
                    {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
                    VK_IMAGE_ASPECT_COLOR_BIT};
    scheduler.RequestOutsideRenderPassOperationContext();
    const u32 index = frame_index++;
    scheduler.Record([this, in, out, hdr, auto_exposure, preset, neural_style, neural_options,
                      index, exposure_image_handle = exposure_image ? *exposure_image : VkImage{},
                      exposure_view_handle = exposure_view ? *exposure_view : VkImageView{},
                      reset = in.reset || options_changed](vk::CommandBuffer cmdbuf) {
        const auto &api = Ngx::GetApi();
        const auto clear_output = [&] {
            cmdbuf.PipelineBarrier(
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                Barrier(out, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_GENERAL));
            cmdbuf.ClearColorImage(out.image, VK_IMAGE_LAYOUT_GENERAL, VkClearColorValue{},
                                   out.range);
            cmdbuf.PipelineBarrier(
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                Barrier(out, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL));
        };
        const auto fail = [&](const char *step, NVSDK_NGX_Result result) {
            evaluations.store(0, std::memory_order_release);
            VideoCommon::OotDlss::NoteBackendFailure();
            if (!failed.exchange(true)) {
                Ngx::ReportFailure(fmt::format("{} failed ({:#x}); using the game's fallback", step,
                                               static_cast<u32>(result)));
            }
            clear_output();
        };
        if (failed) {
            clear_output();
            return;
        }
        // NGX is not thread-safe. Allocate, configure and evaluate on the scheduler worker;
        // feature release on the rendering thread follows Finish() or renderer shutdown.
        if (!parameters) {
            const auto result = api.allocate_parameters(&parameters);
            if (NVSDK_NGX_FAILED(result) || !parameters) {
                fail("parameter allocation", result);
                return;
            }
        }
        if (!feature) {
            NVSDK_NGX_Parameter* capabilities{};
            const auto cap_result = api.capabilities(&capabilities);
            SCOPE_EXIT { if (capabilities) api.destroy_parameters(capabilities); };
            if (NVSDK_NGX_FAILED(cap_result) || !capabilities) {
                fail("capability query", cap_result);
                return;
            }
            namespace Mode = VideoCommon::DlssMode;
            const auto selection = Mode::Select(neural_options.quality,
                {in.render_size.width, in.render_size.height},
                {in.upscale_size.width, in.upscale_size.height},
                [&](Settings::NvidiaDlssQuality mode) {
                    return QueryRange(*capabilities, mode, in.upscale_size);
                });
            if (!selection.mode) {
                fail("unsupported render/output dimensions", NVSDK_NGX_Result_FAIL_InvalidParameter);
                return;
            }
            if (selection.fallback) {
                LOG_WARNING(Render_Vulkan, "NVIDIA DLSS (NGX): {} is unsupported for {}x{} -> {}x{}; using {}",
                    Settings::CanonicalizeEnum(neural_options.quality), in.render_size.width,
                    in.render_size.height, in.upscale_size.width, in.upscale_size.height,
                    Settings::CanonicalizeEnum(*selection.mode));
            }
            last_evaluation.store(static_cast<u32>(*selection.mode), std::memory_order_relaxed);
            const auto mode = ModeFor(*selection.mode);
            parameters->Reset();
            parameters->Set(NVSDK_NGX_Parameter_Width, in.render_size.width);
            parameters->Set(NVSDK_NGX_Parameter_Height, in.render_size.height);
            parameters->Set(NVSDK_NGX_Parameter_OutWidth, in.upscale_size.width);
            parameters->Set(NVSDK_NGX_Parameter_OutHeight, in.upscale_size.height);
            parameters->Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(mode));
            int flags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
            if (in.depth_inverted)
                flags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
            if (hdr)
                flags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
            if (auto_exposure)
                flags |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
            parameters->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, flags);
            parameters->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u);
            parameters->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
            for (const char *key : {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
                                    NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                                    NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
                                    NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                                    NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
                                    NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality}) {
                parameters->Set(key, preset);
            }
            const auto result = api.create(*device.GetLogical(), *cmdbuf,
                                           NVSDK_NGX_Feature_SuperSampling, parameters, &feature);
            if (NVSDK_NGX_FAILED(result) || !feature) {
                fail("feature creation", result);
                return;
            }
        }
        auto color = Resource(in.color, in.render_size);
        auto depth = Resource(in.depth, in.depth_size.width != 0 ? in.depth_size : in.render_size);
        auto motion = Resource(in.motion_vectors, in.render_size);
        auto output = Resource(out, in.upscale_size, true);
        parameters->Set(NVSDK_NGX_Parameter_Color, static_cast<void *>(&color));
        parameters->Set(NVSDK_NGX_Parameter_Depth, static_cast<void *>(&depth));
        parameters->Set(NVSDK_NGX_Parameter_MotionVectors, static_cast<void *>(&motion));
        parameters->Set(NVSDK_NGX_Parameter_Output, static_cast<void *>(&output));
        NVSDK_NGX_Resource_VK bias{};
        if (in.bias_mask) {
            bias = Resource(*in.bias_mask, in.render_size);
        }
        parameters->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask,
                        in.bias_mask ? static_cast<void*>(&bias) : nullptr);
        parameters->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_X, 0u);
        parameters->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_SubrectBase_Y, 0u);
        NVSDK_NGX_Resource_VK exposure{};
        if (in.exposure && exposure_image_handle != VkImage{}) {
            ClearExposure(cmdbuf, exposure_image_handle, *in.exposure);
            const Image image{exposure_view_handle, exposure_image_handle, VK_FORMAT_R32_SFLOAT,
                              {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, VK_IMAGE_ASPECT_COLOR_BIT};
            exposure = Resource(image, {1, 1});
        }
        parameters->Set(NVSDK_NGX_Parameter_ExposureTexture,
                        in.exposure ? static_cast<void*>(&exposure) : nullptr);
        parameters->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, in.jitter[0]);
        parameters->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, in.jitter[1]);
        parameters->Set(NVSDK_NGX_Parameter_MV_Scale_X, in.motion_vector_scale[0]);
        parameters->Set(NVSDK_NGX_Parameter_MV_Scale_Y, in.motion_vector_scale[1]);
        parameters->Set(NVSDK_NGX_Parameter_Reset, reset ? 1 : 0);
        parameters->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,
                        in.render_size.width);
        parameters->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,
                        in.render_size.height);
        parameters->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, 1.0f);
        parameters->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.0f);
        constexpr VkAccessFlags writes = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                         VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        const std::array to_read{
            Barrier(in.color, writes, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            Barrier(in.depth, writes, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            Barrier(in.motion_vectors, writes, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
            Barrier(out, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL)};
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, {}, {}, to_read);
        if (in.bias_mask) {
            cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                   Barrier(*in.bias_mask, writes, VK_ACCESS_SHADER_READ_BIT,
                                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL));
        }
        const auto result = api.evaluate(*cmdbuf, feature, parameters, nullptr);
        const std::array restore{
            Barrier(in.color, VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
            Barrier(in.depth, VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
            Barrier(in.motion_vectors, VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
            Barrier(out, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL)};
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, {}, {}, restore);
        if (NVSDK_NGX_FAILED(result)) {
            fail("evaluation", result);
        } else {
            successful_frame_index.store(index + 1, std::memory_order_release);
            const u32 selected = last_evaluation.load(std::memory_order_relaxed) & 0xff;
            last_evaluation.store(selected | (static_cast<u32>(neural_options.quality) << 8) |
                (static_cast<u32>(VideoCommon::NvidiaDlssModelPreset(preset)) << 16),
                std::memory_order_relaxed);
            evaluations.fetch_add(1, std::memory_order_release);
            Ngx::ReportEvaluation();
            if (neural_rendering) {
                NR::Frame frame;
                frame.style = neural_style;
                frame.intensity = neural_options.strength / 100.0f;
                frame.tone = neural_options.tone / 100.0f;
                frame.structure = neural_options.structure / 100.0f;
                frame.reset = reset;
                frame.hdr = hdr;
                frame.motion_scale = {in.motion_vector_scale[0] / in.render_size.width,
                                      in.motion_vector_scale[1] / in.render_size.height};
                neural_rendering->Record(*cmdbuf, frame);
                NR::ReportFrame();
            }
        }
    });
    scheduler.InvalidateState();
    return true;
}

/// Verify and initialize or rebind neural resources after draining work, serializing queue use
/// and failure cleanup.
void NvidiaDlssUpscaler::PrepareNeuralRendering(const Inputs& in, bool requested) {
    if (requested != neural_requested) {
        neural_requested = requested;
        neural_failed = false;
        if (!requested) {
            // Drain recorded uses before freeing the backend or clearing its status.
            scheduler.Finish();
            std::scoped_lock queue_lock{scheduler.submit_mutex};
            neural_rendering.reset();
            neural_motion_view = VK_NULL_HANDLE;
            NR::ReportInactive();
        }
    }
    if (!neural_requested || neural_failed ||
        (neural_rendering && neural_motion_view == in.motion_vectors.view)) {
        return;
    }
    // The backend uploads model constants during construction. Finish before touching its queue
    // or replacing descriptors referenced by work already recorded on the scheduler worker.
    scheduler.Finish();
    // The presentation thread also uses these queues. Keep construction, rebind failure cleanup
    // and their device-idle waits under the same lock, after Finish has released the worker.
    std::scoped_lock queue_lock{scheduler.submit_mutex};
    try {
        if (neural_rendering) {
            if (neural_rendering->HadTimeout()) {
                throw std::runtime_error("a neural rendering kernel timed out");
            }
            neural_rendering->Rebind(*output_image, *output_view, in.motion_vectors.view);
        } else {
            const auto model = NR::UserFolder();
            if (!neural_model_verified && !NR::IsInstalled(model)) {
                throw std::runtime_error(
                    "import the supported neural rendering model in Graphics settings");
            }
            neural_model_verified = true;
            neural_rendering = std::make_unique<NR::Backend>(
                device.GetInstance(), device.GetPhysical(), *device.GetLogical(),
                device.GetGraphicsFamily(), model, in.upscale_size, *output_image, *output_view,
                in.motion_vectors.view);
            LOG_INFO(Render_Vulkan, "Native FP8 neural rendering ready: {}x{}",
                     in.upscale_size.width, in.upscale_size.height);
            NR::ReportReady();
        }
        neural_motion_view = in.motion_vectors.view;
    } catch (const std::exception& error) {
        neural_rendering.reset();
        neural_failed = true;
        NR::ReportFailure(error.what());
    }
}

VideoCore::DlssStatus NvidiaDlssUpscaler::Status() {
    const auto count = evaluations.load(std::memory_order_acquire);
    const auto last = last_evaluation.load(std::memory_order_relaxed);
    return {.evaluations = count,
            .mode = static_cast<Settings::NvidiaDlssQuality>(last & 0xff),
            .requested = static_cast<Settings::NvidiaDlssQuality>((last >> 8) & 0xff),
            .preset = static_cast<Settings::NvidiaDlssPreset>((last >> 16) & 0xff)};
}

} // namespace Vulkan
