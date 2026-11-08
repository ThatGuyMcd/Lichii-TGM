// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fmt/ranges.h>

#include "common/display_hdr.h"
#include "common/display_refresh.h"
#include "common/dkb_cpu_trace.h"
#include "common/logging.h"
#include <ranges>
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core_timing.h"
#include "core/frontend/graphics_context.h"
#include "video_core/capture.h"
#include "video_core/gpu.h"
#include "video_core/nx2_profile.h"
#include "video_core/present.h"
#include "video_core/render_trace.h"
#include "video_core/renderer_vulkan/present/util.h"
#ifdef HAS_RESHADE
#include "video_core/post_processing/fx_chain.h"
#endif
#include "video_core/hdr_capture.h"
#include "video_core/guest_frame_timing.h"
#include "video_core/oot_dlss.h"
#include "video_core/vulkan_common/amd_ffx.h"
#ifdef _WIN32
#include "video_core/renderer_vulkan/present/amd_frame_gen.h"
#include "video_core/renderer_vulkan/vk_amd_fsr4_upscaler.h"
#endif
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/photo_mode_state.h"
#include "video_core/renderer_vulkan/vk_blit_screen.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#include "video_core/textures/decoders.h"
#include "video_core/vulkan_common/vulkan_debug_callback.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_library.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_surface.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#ifdef EGLANTINE_NVIDIA_DLSS
#include "video_core/vulkan_common/nvidia_dlss.h"
#endif
#ifdef __ANDROID__
#include <jni.h>
#endif
namespace Vulkan {
namespace {

constexpr VkExtent2D CaptureImageSize{
    .width = VideoCore::Capture::LinearWidth,
    .height = VideoCore::Capture::LinearHeight,
};

#ifdef HAS_LSFG
[[nodiscard]] VkExtent2D GuestExtent(std::span<const Tegra::FramebufferConfig> framebuffers) {
    if (framebuffers.empty()) {
        return VkExtent2D{};
    }

    const auto& framebuffer = framebuffers.front();
    if (framebuffer.crop_rect.IsEmpty()) {
        return VkExtent2D{.width = framebuffer.width, .height = framebuffer.height};
    }
    return VkExtent2D{
        .width = static_cast<u32>(framebuffer.crop_rect.GetWidth()),
        .height = static_cast<u32>(framebuffer.crop_rect.GetHeight()),
    };
}
#endif

constexpr VkExtent3D CaptureImageExtent{
    .width = VideoCore::Capture::LinearWidth,
    .height = VideoCore::Capture::LinearHeight,
    .depth = VideoCore::Capture::LinearDepth,
};

constexpr VkFormat CaptureFormat = VK_FORMAT_A8B8G8R8_UNORM_PACK32;

std::string GetReadableVersion(u32 version) {
    return fmt::format("{}.{}.{}", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version),
                       VK_VERSION_PATCH(version));
}

std::string GetDriverVersion(const Device& device) {
    // Extracted from
    // https://github.com/SaschaWillems/vulkan.gpuinfo.org/blob/5dddea46ea1120b0df14eef8f15ff8e318e35462/functions.php#L308-L314
    const u32 version = device.GetDriverVersion();

    if (device.GetDriverID() == VK_DRIVER_ID_NVIDIA_PROPRIETARY) {
        const u32 major = (version >> 22) & 0x3ff;
        const u32 minor = (version >> 14) & 0x0ff;
        const u32 secondary = (version >> 6) & 0x0ff;
        const u32 tertiary = version & 0x003f;
        return fmt::format("{}.{}.{}.{}", major, minor, secondary, tertiary);
    }
    if (device.GetDriverID() == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS) {
        const u32 major = version >> 14;
        const u32 minor = version & 0x3fff;
        return fmt::format("{}.{}", major, minor);
    }
    return GetReadableVersion(version);
}

std::string BuildCommaSeparatedExtensions(
    const std::set<std::string, std::less<>>& available_extensions) {
    return fmt::format("{}", fmt::join(available_extensions, ","));
}

} // Anonymous namespace

Device CreateDevice(const vk::Instance& instance, const vk::InstanceDispatch& dld, VkSurfaceKHR surface) {
    const std::vector<VkPhysicalDevice> devices = instance.EnumeratePhysicalDevices();
    const u32 device_index = Settings::values.vulkan_device.GetValue();
    if (device_index >= u32(devices.size())) {
        LOG_ERROR(Render_Vulkan, "Invalid device index {}!", device_index);
        throw vk::Exception(VK_ERROR_INITIALIZATION_FAILED);
    }
    const vk::PhysicalDevice physical_device(devices[device_index], dld);
    // Before the device: AMD's FSR runtime decides which queues and features it gets.
    static_cast<void>(AmdFfx::Load());
    return Device(*instance, physical_device, surface, dld);
}

RendererVulkan::RendererVulkan(Core::Frontend::EmuWindow& emu_window,
                               Tegra::MaxwellDeviceMemoryManager& device_memory_,
                               Tegra::GPU& gpu_,
                               std::unique_ptr<Core::Frontend::GraphicsContext> context_)
try
    : RendererBase(emu_window, std::move(context_))
    , device_memory(device_memory_)
    , gpu(gpu_)
    , library(OpenLibrary(context.get()))
    , dld()
#ifdef EGLANTINE_NVIDIA_DLSS
    , nvidia_dlss_started(NvidiaDlss::Start())
#endif
    // Create raw Vulkan instance first
    , instance(CreateInstance(*library,
                            dld,
                            VK_API_VERSION_1_1,
                            render_window.GetWindowInfo().type,
                            Settings::values.renderer_debug.GetValue()))
    // Create debug messenger if debug is enabled
    , debug_messenger(Settings::values.renderer_debug ? CreateDebugUtilsCallback(instance)
                                                    : vk::DebugUtilsMessenger{})
    // Create surface
    , surface(CreateSurface(instance, render_window.GetWindowInfo()))
    , device(CreateDevice(instance, dld, *surface))
    , memory_allocator(device)
    , state_tracker()
    , scheduler(device, state_tracker)
    , swapchain(*surface,
                render_window.GetWindowInfo().render_surface,
                device,
                scheduler,
               render_window.GetFramebufferLayout().width,
               render_window.GetFramebufferLayout().height)
    , present_manager(instance,
                      render_window,
                      device,
                      memory_allocator,
                      scheduler,
                      swapchain,
                      surface)
    , blit_swapchain(device_memory,
                   device,
                   memory_allocator,
                   present_manager,
                   scheduler,
                   PresentFiltersForDisplay)
    , blit_capture(device_memory,
                   device,
                   memory_allocator,
                   present_manager,
                   scheduler,
                   PresentFiltersForDisplay)
    , blit_applet(device_memory,
                  device,
                  memory_allocator,
                  present_manager,
                  scheduler,
                  PresentFiltersForAppletCapture)
    , rasterizer(render_window, gpu, device_memory, device, memory_allocator, state_tracker, scheduler)
#ifdef HAS_LSFG
    , frame_gen(memory_allocator, scheduler)
#endif
{

    if (Settings::values.renderer_force_max_clock.GetValue() && device.ShouldBoostClocks()) {
        turbo_mode.emplace(instance, dld);
        scheduler.RegisterOnSubmit([this] { turbo_mode->QueueSubmitted(); });
    }

#ifdef HAS_RESHADE
    VideoCore::FxChain::Instance().LoadFromSettings();
#endif

    present_manager.SetBeforePresent([](const Frame& frame) {
        VideoCore::GuestFrameTiming::RecordPresent({frame.guest_frame, frame.guest_start_ns});
    });

#ifdef _WIN32
    // Ocarina of Time's AMD modes run on the HDR image only with FSR 4 (oot_dlss.h HdrInput).
    if (VideoCommon::OotDlss::UsesAmd()) {
        VideoCommon::OotDlss::AmdFsr4Available().store(AmdFsr4Upscaler::IsAvailable(device),
                                                       std::memory_order_relaxed);
    }
#endif

    device_info = {
        .model = std::string{device.GetModelName()},
        .driver = device.GetDriverVersion() != 0
                      ? fmt::format("{} {}", device.GetVendorName(), GetDriverVersion(device))
                      : std::string{},
        .api = "Vulkan",
    };
    Report();
} catch (const vk::Exception& exception) {
    LOG_ERROR(Render_Vulkan, "Vulkan initialization failed with error: {}", exception.what());
    throw std::runtime_error{fmt::format("Vulkan initialization error {}", exception.what())};
}

RendererVulkan::~RendererVulkan() {
    scheduler.RegisterOnSubmit([] {});
    // Members are destroyed before the scheduler: run the commands that still point into them,
    // such as frame generation's optical flow copies.
    scheduler.WaitWorker();
    void(device.GetLogical().WaitIdle());
}

void RendererVulkan::UpdatePacing() {
    const auto now = std::chrono::steady_clock::now();
    if (now - display_checked < std::chrono::milliseconds{500}) {
        return;
    }
    display_checked = now;
    void* const window = render_window.GetWindowInfo().render_surface;
    covers_monitor = Common::WindowCoversMonitor(window);
    refresh_hz = Common::QueryDisplayRefreshRate(window).value_or(0);
    SCOPE_EXIT {
        // The status the debug server reads, gathered here on the presenting thread.
        VideoCore::PresentStatus status{
            .refresh_hz = refresh_hz,
            .frame_cap = swapchain.GetFrameCap(),
        };
#ifdef _WIN32
        if (const AmdFrameGen* const amd_frame_gen = swapchain.GetAmdFrameGen()) {
            const AmdFrameGenPacing pacing = amd_frame_gen->Pacing();
            status.amd_frame_gen = true;
            status.fg_swapchain = pacing.swapchain_provider;
            status.fg_generation = pacing.frame_gen_provider;
            status.driver = pacing.driver;
            status.fg_rdna4 = pacing.rdna4;
            status.fg_driver_26_10_1 = pacing.driver_26_10_1;
            status.fg_swapchain_4 = pacing.swapchain_4;
            status.fg_vsync_off = pacing.vsync_off;
            status.fg_covers_monitor = pacing.covers_monitor;
            status.fg_hags = pacing.hags;
            status.fg_generating = pacing.frame_generation;
            status.fg_driver_pacing_expected = pacing.DriverPacingExpected();
            status.fg_presents = pacing.presents;
            if (status.fg_driver_pacing_expected != logged_driver_pacing) {
                logged_driver_pacing = status.fg_driver_pacing_expected;
                LOG_INFO(Render_Vulkan,
                         "AMD FSR frame generation: {} ({}, {}; RDNA 4 {}, driver {} {}, VSync "
                         "off {}, fullscreen {}, HAGS {})",
                         *logged_driver_pacing ? "AMD's driver paces the flips"
                                              : "AMD's swapchain paces in software",
                         status.fg_swapchain, status.fg_generation, status.fg_rdna4,
                         status.driver, status.fg_driver_26_10_1, status.fg_vsync_off,
                         status.fg_covers_monitor, status.fg_hags);
            }
        }
#endif
        std::scoped_lock lock{present_status_mutex};
        present_status = std::move(status);
    };
}

VideoCore::PresentStatus RendererVulkan::GetPresentStatus() const {
    std::scoped_lock lock{present_status_mutex};
    return present_status;
}

void RendererVulkan::Composite(std::span<const Tegra::FramebufferConfig> framebuffers) {
    const bool trace = Common::DkbFrameTrace::Enabled();
    const auto entered = trace ? std::chrono::steady_clock::now()
                               : std::chrono::steady_clock::time_point{};
    SCOPE_EXIT {
        render_window.OnFrameDisplayed();
    };

    RenderAppletCaptureLayer(framebuffers);

    // Ocarina of Time's photo mode, hotsampling: only the centred crop of the game's largest layer
    // at the picture ratio, so the window (sized to it by the frontend) and screenshots get it
    // pixel for pixel.
    std::vector<Tegra::FramebufferConfig> cropped_layers;
    if (const u64 crop = VideoCore::PhotoMode::PresentCrop(); crop != 0 && !framebuffers.empty()) {
        cropped_layers.assign(framebuffers.begin(), framebuffers.end());
        auto largest = cropped_layers.begin();
        for (auto it = cropped_layers.begin(); it != cropped_layers.end(); ++it) {
            if (u64{it->width} * it->height > u64{largest->width} * largest->height) {
                largest = it;
            }
        }
        const auto& old_crop = largest->crop_rect;
        const int left = old_crop.IsEmpty() ? 0 : old_crop.left;
        const int top = old_crop.IsEmpty() ? 0 : old_crop.top;
        const u64 width = old_crop.IsEmpty() ? largest->width : old_crop.GetWidth();
        const u64 height = old_crop.IsEmpty() ? largest->height : old_crop.GetHeight();
        const u64 ratio_width = crop & 0xffffffff;
        const u64 ratio_height = crop >> 32;
        u64 crop_width = width;
        u64 crop_height = height;
        if (width * ratio_height > height * ratio_width) {
            crop_width = height * ratio_width / ratio_height;
        } else {
            crop_height = width * ratio_height / ratio_width;
        }
        const int x = left + static_cast<int>((width - crop_width) / 2);
        const int y = top + static_cast<int>((height - crop_height) / 2);
        largest->crop_rect = Common::Rectangle<int>{x, y, x + static_cast<int>(crop_width),
                                                    y + static_cast<int>(crop_height)};
        framebuffers = cropped_layers;
    }
    {
        // The game's own frame size (its largest layer, after any crop), for full-size screenshots.
        u64 best_area = 0;
        u32 best_width = 0;
        u32 best_height = 0;
        for (const auto& framebuffer : framebuffers) {
            const auto& crop = framebuffer.crop_rect;
            const u32 width =
                crop.GetWidth() > 0 ? static_cast<u32>(crop.GetWidth()) : framebuffer.width;
            const u32 height =
                crop.GetHeight() > 0 ? static_cast<u32>(crop.GetHeight()) : framebuffer.height;
            if (u64{width} * height > best_area) {
                best_area = u64{width} * height;
                best_width = width;
                best_height = height;
            }
        }
        if (best_area != 0) {
            VideoCore::PhotoMode::SetPresentedFrameSize(best_width, best_height);
        }
    }

    if (!render_window.IsShown()) {
        return;
    }

    if (VideoCommon::StutterTrace::Enabled()) {
        VideoCommon::StutterTrace::OnPresent();
    }
    RenderScreenshot(framebuffers);
    const auto acquiring = trace ? std::chrono::steady_clock::now()
                                 : std::chrono::steady_clock::time_point{};
    Frame* frame = [this] {
        const VideoCommon::Nx2Profile::Scope scope{VideoCommon::Nx2Profile::Zone::Present};
        return present_manager.GetRenderFrame();
    }();
    const auto acquired = trace ? std::chrono::steady_clock::now()
                                : std::chrono::steady_clock::time_point{};

#ifdef __ANDROID__
    VkFormat output_format = VK_FORMAT_R8G8B8A8_UNORM;
#else
    VkFormat output_format = VK_FORMAT_B8G8R8A8_UNORM;
#endif
    const bool hdr_output =
        Settings::values.hdr_mode.GetValue() != Settings::HdrMode::Off && framebuffers.size() == 1 &&
        framebuffers.front().color_space != Tegra::ColorSpace::RGB709;
    if (hdr_output) {
        // The guest sees HDR availability, not individual encodings. Presentation must reject an
        // unavailable pair rather than reinterpret an already-rendered HDR frame as SDR.
        switch (framebuffers.front().color_space) {
        case Tegra::ColorSpace::RGB2020_PQ:
            output_format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            frame->color_space = VK_COLOR_SPACE_HDR10_ST2084_EXT;
            break;
        case Tegra::ColorSpace::RGB709_Linear:
            output_format = VK_FORMAT_R16G16B16A16_SFLOAT;
            frame->color_space = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
            break;
        default:
            break;
        }
    }

    scheduler.RequestOutsideRenderPassOperationContext();
    blit_swapchain.DrawToFrame(device, rasterizer, frame, framebuffers,
                              render_window.GetFramebufferLayout(),
                              present_manager.SwapchainImageCount(), output_format, hdr_output);

    VkSemaphore flow_wait = VK_NULL_HANDLE;
    if (!framebuffers.empty()) {
        frame->guest_frame = framebuffers.front().guest_frame;
        frame->guest_start_ns = framebuffers.front().guest_start_ns;
    }
    UpdatePacing();
#ifdef _WIN32
    if (AmdFrameGen* const amd_frame_gen = swapchain.GetAmdFrameGen()) {
        // The game's area of the presented image, in the swapchain's pixels.
        const auto& layout = render_window.GetFramebufferLayout();
        const VkExtent2D size = swapchain.GetExtent();
        const f32 sx = layout.width ? static_cast<f32>(size.width) / static_cast<f32>(layout.width)
                                    : 1.0f;
        const f32 sy = layout.height
                           ? static_cast<f32>(size.height) / static_cast<f32>(layout.height)
                           : 1.0f;
        amd_frame_gen->SetGenerationRect(
            static_cast<s32>(static_cast<f32>(layout.screen.left) * sx),
            static_cast<s32>(static_cast<f32>(layout.screen.top) * sy),
            static_cast<s32>(static_cast<f32>(layout.screen.GetWidth()) * sx),
            static_cast<s32>(static_cast<f32>(layout.screen.GetHeight()) * sy));
        amd_frame_gen->SetPresentationState(
            covers_monitor, swapchain.GetPresentMode() == VK_PRESENT_MODE_IMMEDIATE_KHR);
    }
#endif

#ifdef HAS_LSFG
    if (hdr_output) {
        frame_gen.Pause();
    } else {
        void(frame_gen.WantedGenerations(present_manager.MaxExtraFrames()));

        frame_gen.Process(device, frame, frame->image_format, GuestExtent(framebuffers));

    const size_t generated_frames = frame_gen.GeneratedFrameCount();
    // Each generated frame keeps the screen for an equal share of the real frame interval. Sent
    // back to back, a high refresh rate display shows it for one refresh only.
    const auto spacing = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<f32>(std::min(frame_gen.FrameInterval(), 0.1f) /
                                   static_cast<f32>(generated_frames + 1)));
    // The first submission after the optical flow waits for it.
    flow_wait = frame_gen.TakeFlowWait();
    for (size_t generation = 0; generation < generated_frames; ++generation) {
        Frame* generated = present_manager.GetRenderFrame();
        generated->present_spacing = spacing;
        blit_swapchain.PrepareFrame(device, generated, render_window.GetFramebufferLayout());
        frame_gen.GenerateInto(device, generated, generation);
        if (!generated->generated_async) {
            // Generated on the graphics queue (or not at all): this submission signals it.
            scheduler.Flush(*generated->render_ready, std::exchange(flow_wait, VK_NULL_HANDLE));
        }
        frame_gen.DumpInterpolation(generated);
        present_manager.Present(generated);
    }
    }
#endif

    if (trace) {
        scheduler.MarkFrameEnd(++trace_frame);
    }
    scheduler.Flush(*frame->render_ready, flow_wait);

    const auto presenting = trace ? std::chrono::steady_clock::now()
                                  : std::chrono::steady_clock::time_point{};
    {
        const VideoCommon::Nx2Profile::Scope scope{VideoCommon::Nx2Profile::Zone::Present};
        present_manager.Present(frame);
    }
    const auto presented = trace ? std::chrono::steady_clock::now()
                                 : std::chrono::steady_clock::time_point{};
#ifdef HAS_LSFG
    scheduler.DispatchWork();
#endif

    gpu.RendererFrameEndNotify();
    rasterizer.TickFrame();
    if (trace) {
        const auto end = std::chrono::steady_clock::now();
        const auto ms = [](auto duration) {
            return std::chrono::duration<double, std::milli>(duration).count();
        };
        if (trace_cpu_rate.Ready(end)) {
            LOG_INFO(Render_Vulkan,
                     "DKB renderer renderer_frame={} host_pacing_mode={} vsync_mode={} "
                     "cpu_frame_wall_ms={:.3f} "
                     "cpu_composite_wall_ms={:.3f} capture_wall_ms={:.3f} "
                     "acquire_frame_wall_ms={:.3f} record_submit_wall_ms={:.3f} "
                     "present_call_wall_ms={:.3f} frame_end_wall_ms={:.3f}",
                     trace_frame, static_cast<int>(Settings::values.frame_pacing_mode.GetValue()),
                     static_cast<int>(Settings::values.vsync_mode.GetValue()),
                     trace_last_frame == std::chrono::steady_clock::time_point{}
                         ? -1.0 : ms(entered - trace_last_frame),
                     ms(end - entered), ms(acquiring - entered), ms(acquired - acquiring),
                     ms(presenting - acquired), ms(presented - presenting), ms(end - presented));
        }
        trace_last_frame = entered;
    }
}

void RendererVulkan::Report() const {
    using namespace Common::Literals;

    const std::string api_version = GetReadableVersion(device.ApiVersion());

    const std::string extensions = BuildCommaSeparatedExtensions(device.GetAvailableExtensions());

    const auto available_vram = static_cast<f64>(device.GetDeviceLocalMemory()) / f64{1_GiB};

    LOG_INFO(Render_Vulkan, "Driver: {} {}", device.GetVendorName(), GetDriverVersion(device));
    LOG_INFO(Render_Vulkan, "Device: {}", device_info.model);
    LOG_INFO(Render_Vulkan, "Vulkan: {}", api_version);
    LOG_INFO(Render_Vulkan, "Available VRAM: {:.2f} GiB", available_vram);
    if (VideoCommon::RenderTrace::Enabled()) {
        LOG_INFO(Render_Vulkan, "Render trace enabled: interval=600 frames, log_filter={}",
                 Settings::values.log_filter.GetValue());
    }
    LOG_DEBUG(Render_Vulkan,
              "Render trace capabilities: geometry_shader={} transform_feedback={} "
              "geometry_streams={} mesh_shader={}",
              device.GetPhysical().GetFeatures().geometryShader, device.IsExtTransformFeedbackSupported(),
              device.AreTransformFeedbackGeometryStreamsSupported(), device.IsExtMeshShaderSupported());
}

vk::Buffer RendererVulkan::RenderToBuffer(std::span<const Tegra::FramebufferConfig> framebuffers,
                                          const Layout::FramebufferLayout& layout, VkFormat format,
                                          VkDeviceSize buffer_size, bool hdr_output) {
    auto& frame = screenshot_frame;
    auto dst_buffer = CreateWrappedBuffer(memory_allocator, buffer_size, MemoryUsage::Download);
    blit_capture.DrawToFrame(device, rasterizer, &frame, framebuffers, layout, 1, format,
                             hdr_output);

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([&](vk::CommandBuffer cmdbuf) {
        DownloadColorImage(cmdbuf, *frame.image, *dst_buffer,
                           VkExtent3D{layout.width, layout.height, 1});
    });

    // Ensure the copy is fully completed before saving the capture
    scheduler.Finish();

    // Copy backing image data to the capture buffer
    dst_buffer.Invalidate();
    return dst_buffer;
}

void RendererVulkan::RenderScreenshot(std::span<const Tegra::FramebufferConfig> framebuffers) {
    if (!renderer_settings.screenshot_requested) {
        return;
    }

    const auto screenshot_layers = Tegra::FilterLayerStack(
        framebuffers, renderer_settings.screenshot_layer_stack, screenshot_layer_scratch);

    const auto& layout{renderer_settings.screenshot_framebuffer_layout};
    const size_t pixels = size_t{layout.width} * layout.height;
    const auto hdr_layer = std::ranges::find_if(screenshot_layers, [](const auto& framebuffer) {
        return framebuffer.color_space != Tegra::ColorSpace::RGB709;
    });
    if (hdr_layer != screenshot_layers.end()) {
        // The HDR image drawn unchanged into a float frame, then converted to what the game's
        // SDR output would look like (an 8-bit copy of PQ codes looks grey and washed out).
        const auto dst_buffer = RenderToBuffer(screenshot_layers, layout,
                                               VK_FORMAT_R16G16B16A16_SFLOAT, pixels * 8, true);
        const auto output = Common::ResolveHdrOutput(
            Common::QueryDisplayHdr(render_window.GetWindowInfo().render_surface),
            Settings::HdrMode::On, Settings::values.hdr_peak_luminance.GetValue(),
            Settings::values.hdr_reference_white.GetValue());
        const std::span<const u8> mapped = dst_buffer.Mapped();
        VideoCore::HdrCapture::Convert(
            std::span{reinterpret_cast<const u16*>(mapped.data()), pixels * 4},
            std::span{static_cast<u8*>(renderer_settings.screenshot_bits), pixels * 4},
            {
                .color_space = hdr_layer->color_space,
                .reference_white = static_cast<f32>(output.reference_white),
                .nits_per_unit = hdr_layer->linear_unit_luminance != 0
                                     ? static_cast<f32>(hdr_layer->linear_unit_luminance)
                                     : 80.0f,
            });
    } else {
        const auto dst_buffer =
            RenderToBuffer(screenshot_layers, layout, VK_FORMAT_B8G8R8A8_UNORM, pixels * 4);
        std::memcpy(renderer_settings.screenshot_bits, dst_buffer.Mapped().data(),
                    dst_buffer.Mapped().size());
    }
    renderer_settings.screenshot_complete_callback(false);
    renderer_settings.screenshot_requested = false;
}

std::vector<u8> RendererVulkan::GetAppletCaptureBuffer() {
    using namespace VideoCore::Capture;

    std::vector<u8> out(VideoCore::Capture::TiledSize);

    if (!applet_frame.image) {
        return out;
    }

    const auto dst_buffer =
        CreateWrappedBuffer(memory_allocator, VideoCore::Capture::TiledSize, MemoryUsage::Download);

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([&](vk::CommandBuffer cmdbuf) {
        DownloadColorImage(cmdbuf, *applet_frame.image, *dst_buffer, CaptureImageExtent);
    });

    // Ensure the copy is fully completed before writing the capture
    scheduler.Finish();

    // Swizzle image data to the capture buffer
    dst_buffer.Invalidate();
    Tegra::Texture::SwizzleTexture(out, dst_buffer.Mapped(), BytesPerPixel, LinearWidth,
                                   LinearHeight, LinearDepth, BlockHeight, BlockDepth);

    return out;
}

void RendererVulkan::RenderAppletCaptureLayer(
    std::span<const Tegra::FramebufferConfig> framebuffers) {
    const auto capture_layers = Tegra::FilterLayerStack(
        framebuffers, Service::Nvnflinger::LayerStackId::LastFrame, applet_capture_layers);

    if (capture_layers.empty())
        return;

    if (!applet_frame.image) {
        applet_frame.image = CreateWrappedImage(memory_allocator, CaptureImageSize, CaptureFormat);
        applet_frame.image_view = CreateWrappedImageView(device, applet_frame.image, CaptureFormat);
        applet_frame.framebuffer = blit_applet.CreateFramebuffer(device,
            VideoCore::Capture::Layout, *applet_frame.image_view, CaptureFormat);
    }

    scheduler.RequestOutsideRenderPassOperationContext();
    blit_applet.DrawToFrame(device, rasterizer, &applet_frame, capture_layers, VideoCore::Capture::Layout, 1,
                            CaptureFormat, false);
}

} // namespace Vulkan
