// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <chrono>

namespace VideoCore {
class ShaderNotify {
public:
    [[nodiscard]] int ShadersBuilding() noexcept;

    [[nodiscard]] int ShadersLeft() const noexcept {
        return num_building.load(std::memory_order_relaxed) -
               num_complete.load(std::memory_order_relaxed);
    }

    void MarkShaderComplete() noexcept {
        ++num_complete;
    }

    void MarkShaderBuilding() noexcept {
        ++num_building;
    }

private:
    std::atomic_int num_building{};
    std::atomic_int num_complete{};
    int report_base{};

    bool completed{};
    int num_when_completed{};
    std::chrono::steady_clock::time_point complete_time;
};

/// Balances a pipeline's notification when its build runs, is cancelled, or construction fails.
/// The notifier must outlive this object; pipeline jobs must finish before its destruction.
class ShaderBuildNotification {
public:
    explicit ShaderBuildNotification(ShaderNotify* notify_) noexcept : notify{notify_} {
        if (notify) {
            notify->MarkShaderBuilding();
        }
    }

    ~ShaderBuildNotification() {
        Complete();
    }

    ShaderBuildNotification(const ShaderBuildNotification&) = delete;
    ShaderBuildNotification& operator=(const ShaderBuildNotification&) = delete;

    void Complete() noexcept {
        if (notify && !completed.exchange(true, std::memory_order_relaxed)) {
            notify->MarkShaderComplete();
        }
    }

private:
    ShaderNotify* const notify;
    std::atomic_bool completed{false};
};
} // namespace VideoCore
