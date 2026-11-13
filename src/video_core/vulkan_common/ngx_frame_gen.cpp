// SPDX-License-Identifier: GPL-3.0-or-later
// NGX's parameter interface, without replacing the pinned SDK or dynamic core ABI.
// Parameter contracts: NVIDIA/DLSS include/nvsdk_ngx_defs_dlssg.h.
#include "video_core/vulkan_common/ngx_frame_gen.h"
#include "video_core/vulkan_common/ngx_api.h"
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace Vulkan::Ngx {
namespace {
struct Config {
    u32 width, height, format, render_width, render_height;
    bool operator==(const Config&) const = default;
};
NVSDK_NGX_Resource_VK Resource(const NvidiaDlssImage& image, VkExtent2D size, bool write) {
    NVSDK_NGX_Resource_VK resource{};
    resource.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    resource.Resource.ImageViewInfo = {image.view, image.image, image.range, image.format,
                                       size.width, size.height};
    resource.ReadWrite = write;
    return resource;
}
}
struct FrameGenFeature::State {
    NVSDK_NGX_Handle* handle{};
    NVSDK_NGX_Parameter* parameters{};
    std::optional<Config> config;
    NVSDK_NGX_Result result{};
    NVSDK_NGX_Result create_result{};
    bool just_created{};
    bool create_failed{};
};
FrameGenFeature::FrameGenFeature() : state{std::make_unique<State>()} {}
FrameGenFeature::~FrameGenFeature() { Release(); }
u32 FrameGenFeature::LastResult() const { return static_cast<u32>(state->result); }
u32 FrameGenFeature::CreateResult() const { return static_cast<u32>(state->create_result); }
bool FrameGenFeature::JustCreated() const { return state->just_created; }
bool FrameGenFeature::CreateFailed() const { return state->create_failed; }
void FrameGenFeature::Release() {
    const auto& api = GetApi();
    if (state->handle) api.release(state->handle);
    if (state->parameters) api.destroy_parameters(state->parameters);
    state->handle = nullptr;
    state->parameters = nullptr;
    state->config.reset();
}
bool FrameGenFeature::Evaluate(VkCommandBuffer command, const FrameGenInputs& in) {
    state->just_created = false;
    if (!IsFrameGenAvailable() || !in.render_size.width || !in.render_size.height ||
        !in.backbuffer_size.width || !in.backbuffer_size.height) return false;
    const auto& api = GetApi();
    const auto size = in.subrect_size.width && in.subrect_size.height ? in.subrect_size
                                                                    : in.backbuffer_size;
    const Config config{size.width, size.height, static_cast<u32>(in.backbuffer.format),
                         in.render_size.width, in.render_size.height};
    if (!state->config || *state->config != config) {
        Release();
        state->create_failed = true;
        state->create_result = state->result = api.allocate_parameters(&state->parameters);
        if (NVSDK_NGX_FAILED(state->result)) return false;
        auto* p = state->parameters;
        p->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1U);
        p->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1U);
        p->Set(NVSDK_NGX_Parameter_Width, config.width);
        p->Set(NVSDK_NGX_Parameter_Height, config.height);
        p->Set("DLSSG.Width", config.width);
        p->Set("DLSSG.Height", config.height);
        p->Set("DLSSG.BackbufferFormat", config.format);
        p->Set("DLSSG.InternalWidth", config.render_width);
        p->Set("DLSSG.InternalHeight", config.render_height);
        p->Set("DLSSG.DynamicResolution", 0U);
        state->create_result = state->result = api.create(
            FrameGenDevice(), command, NVSDK_NGX_Feature_FrameGeneration, p, &state->handle);
        if (NVSDK_NGX_FAILED(state->result)) return false;
        state->create_failed = false;
        state->just_created = true;
        state->config = config;
    }
    auto backbuffer = Resource(in.backbuffer, in.backbuffer_size, false);
    auto depth = Resource(in.depth, in.render_size, false);
    auto motion = Resource(in.motion_vectors, in.render_size, false);
    auto output = Resource(in.output, in.backbuffer_size, true);
    auto* p = state->parameters;
    p->Set("DLSSG.Backbuffer", static_cast<void*>(&backbuffer));
    p->Set("DLSSG.Depth", static_cast<void*>(&depth));
    p->Set("DLSSG.MVecs", static_cast<void*>(&motion));
    p->Set("DLSSG.OutputInterpolated", static_cast<void*>(&output));
    p->Set("DLSSG.MultiFrameCount", std::max(in.multi_frame_count, 1U));
    p->Set("DLSSG.MultiFrameIndex", std::clamp(in.multi_frame_index, 1U,
                                             std::max(in.multi_frame_count, 1U)));
    p->Set("DLSSG.BackbufferFrameID", static_cast<unsigned long long>(in.frame_id));
    std::array<f32, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (const char* name : {"DLSSG.CameraViewToClip", "DLSSG.ClipToCameraView",
                             "DLSSG.ClipToLensClip", "DLSSG.ClipToPrevClip",
                             "DLSSG.PrevClipToClip"}) p->Set(name, static_cast<void*>(identity.data()));
    p->Set("DLSSG.JitterOffsetX", in.jitter[0]);
    p->Set("DLSSG.JitterOffsetY", in.jitter[1]);
    p->Set("DLSSG.MvecScaleX", in.motion_vector_scale[0] / in.render_size.width);
    p->Set("DLSSG.MvecScaleY", in.motion_vector_scale[1] / in.render_size.height);
    p->Set("DLSSG.CameraUpY", 1.0f);
    p->Set("DLSSG.CameraRightX", 1.0f);
    p->Set("DLSSG.CameraFwdZ", 1.0f);
    p->Set("DLSSG.CameraNear", 0.1f);
    p->Set("DLSSG.CameraFar", 1000.0f);
    p->Set("DLSSG.CameraFOV", 1.0f);
    p->Set("DLSSG.CameraAspectRatio", static_cast<f32>(in.render_size.width) / in.render_size.height);
    p->Set("DLSSG.ColorBuffersHDR", static_cast<u32>(in.color_hdr));
    p->Set("DLSSG.DepthInverted", static_cast<u32>(in.depth_inverted));
    p->Set("DLSSG.CameraMotionIncluded", 1U);
    p->Set("DLSSG.Reset", static_cast<u32>(in.reset || state->just_created));
    // Subrect parameter names are scalar coordinates/dimensions in the NGX interface.
    for (const char* image : {"MVecs", "Depth", "InputBackbuffer", "OutputInterpolated"}) {
        const bool input = std::string_view{image} == "MVecs" || std::string_view{image} == "Depth";
        const std::string prefix = std::string{"DLSSG."} + image + "Subrect";
        p->Set((prefix + "BaseX").c_str(), input ? 0U : static_cast<u32>(in.subrect_offset.x));
        p->Set((prefix + "BaseY").c_str(), input ? 0U : static_cast<u32>(in.subrect_offset.y));
        p->Set((prefix + "Width").c_str(), input ? in.render_size.width : size.width);
        p->Set((prefix + "Height").c_str(), input ? in.render_size.height : size.height);
    }
    state->result = api.evaluate(command, state->handle, p, nullptr);
    return NVSDK_NGX_SUCCEED(state->result);
}
}
