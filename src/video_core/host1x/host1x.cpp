// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/core.h"
#include "video_core/host1x/host1x.h"
#include "video_core/host1x/nvdec.h"
#include "video_core/host1x/vic.h"

namespace Tegra::Host1x {

Host1x::Host1x(Core::System& system_)
    : system{system_}
    , syncpoint_manager{}
    , memory_manager(system.DeviceMemory())
    , gmmu_manager{system, memory_manager, 32, 0, 12}
    , allocator{1 << 12}
{}

Host1x::~Host1x() {
    for (s32 fd = 0; fd < static_cast<s32>(devices.size()); ++fd) {
        StopDeviceThread(fd);
    }
}

void Host1x::StartDevice(s32 fd, ChannelType type, u32 syncpt) {
    if (!ValidFd(fd)) {
        LOG_ERROR(HW_GPU, "No Host1x device for fd {} ({} slots)", fd, devices.size());
        return;
    }
    switch (type) {
    case ChannelType::NvDec:
#ifdef YUZU_LEGACY
        std::call_once(nvdec_first_init, []() {std::this_thread::sleep_for(std::chrono::milliseconds{500});}); // HACK: For Astroneer
#endif
        StopDeviceThread(fd);
        devices[fd].emplace<Tegra::Host1x::Nvdec>(*this, fd, syncpt);
        break;
    case ChannelType::VIC:
#ifdef YUZU_LEGACY
        std::call_once(vic_first_init, []() {std::this_thread::sleep_for(std::chrono::milliseconds{500});}); // HACK: For Astroneer
#endif
        StopDeviceThread(fd);
        devices[fd].emplace<Tegra::Host1x::Vic>(*this, fd, syncpt);
        break;
    default:
        LOG_ERROR(HW_GPU, "Unimplemented host1x device {}", u32(type));
        break;
    }
}

void Host1x::StopDevice(s32 fd, ChannelType type) {
    if (!ValidFd(fd)) {
        return;
    }
    StopDeviceThread(fd);
    devices[fd].emplace<std::monostate>();
}

void Host1x::StopDeviceThread(s32 fd) {
    // The worker can still dispatch virtual methods while a stop is in flight.
    // Keep the active variant alive until the worker has joined.
    if (auto* nvdec = std::get_if<Nvdec>(&devices[fd])) {
        nvdec->StopThread();
    } else if (auto* vic = std::get_if<Vic>(&devices[fd])) {
        vic->StopThread();
    }
}

} // namespace Tegra::Host1x
