// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <utility>

namespace Vulkan {

// A bank may be destroyed before its reset reaches the Vulkan worker. Keep the pool, rather
// than the bank or its raw handle, until the recorded reset has run or is discarded.
template <typename Pool, typename Reset>
auto MakeQueryPoolReset(std::shared_ptr<Pool> pool, Reset reset) {
    return [pool = std::move(pool), reset = std::move(reset)](auto cmdbuf) {
        reset(*pool, cmdbuf);
    };
}

} // namespace Vulkan
