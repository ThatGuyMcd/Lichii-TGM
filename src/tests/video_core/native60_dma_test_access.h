// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "video_core/dma_pusher.h"

namespace Tegra {
struct Native60DmaPusherTestAccess {
    static bool Step(DmaPusher& pusher) {
        return pusher.Step();
    }
    static size_t Process(DmaPusher& pusher, std::span<const CommandHeader> words) {
        return pusher.ProcessCommands(words);
    }
    static u32 DmaRegister(const DmaPusher& pusher, u32 method) {
        return pusher.dma_registers[method];
    }
    static u32 InlineToMemoryRegister(const DmaPusher& pusher, u32 method) {
        return pusher.inline_to_memory_registers[method];
    }
};
} // namespace Tegra
