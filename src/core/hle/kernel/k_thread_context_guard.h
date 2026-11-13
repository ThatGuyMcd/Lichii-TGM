// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>

namespace Kernel {

// Adapted from Lichii #717. The scheduler only tries and releases this guard; it never sleeps
// inside it. A failed try avoids another exchange while the saved context is still in use.
class KThreadContextGuard {
public:
    bool try_lock() noexcept {
        return !locked.load(std::memory_order_relaxed) &&
               !locked.exchange(true, std::memory_order_acquire);
    }

    void unlock() noexcept {
        locked.store(false, std::memory_order_release);
    }

private:
    std::atomic<bool> locked{};
};

} // namespace Kernel
