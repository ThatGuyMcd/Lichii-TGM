// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include "video_core/nvidia_dlss_inputs.h"

namespace Vulkan::Ngx {
[[nodiscard]] bool IsFrameGenAvailable();
[[nodiscard]] u32 FrameGenMultiplier();
[[nodiscard]] VkDevice FrameGenDevice();
void DropFrameGen(const char* reason);

struct FrameGenInputs {
    NvidiaDlssImage backbuffer;
    VkExtent2D backbuffer_size;
    VkOffset2D subrect_offset;
    VkExtent2D subrect_size;
    NvidiaDlssImage depth;
    NvidiaDlssImage motion_vectors;
    VkExtent2D render_size;
    NvidiaDlssImage output;
    std::array<f32, 2> jitter;
    std::array<f32, 2> motion_vector_scale;
    bool reset;
    bool depth_inverted;
    bool color_hdr;
    u32 multi_frame_count;
    u32 multi_frame_index;
    u64 frame_id;
};

class FrameGenFeature {
public:
    FrameGenFeature();
    ~FrameGenFeature();
    bool Evaluate(VkCommandBuffer command, const FrameGenInputs& inputs);
    [[nodiscard]] u32 LastResult() const;
    [[nodiscard]] bool JustCreated() const;
    [[nodiscard]] u32 CreateResult() const;
    [[nodiscard]] bool CreateFailed() const;
    void Release();
private:
    struct State;
    std::unique_ptr<State> state;
};
}
