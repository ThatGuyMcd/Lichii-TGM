// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <exception>

#include "common/logging.h"
#include "common/scope_exit.h"
#include "video_core/host1x/syncpoint_manager.h"

namespace Tegra {

namespace Host1x {

SyncpointManager::ActionHandle SyncpointManager::RegisterAction(
    std::atomic<u32>& syncpoint, std::list<RegisteredAction>& action_storage, u32 expected_value,
    std::function<void()>&& action) {
    if (syncpoint.load(std::memory_order_acquire) >= expected_value) {
        action();
        return {};
    }

    {
        std::scoped_lock lk(guard);
        if (syncpoint.load(std::memory_order_relaxed) < expected_value) {
            auto it = action_storage.begin();
            while (it != action_storage.end() && it->expected_value < expected_value) {
                ++it;
            }
            return action_storage.emplace(it, expected_value, ++last_action_id, std::move(action))
                ->id;
        }
    }
    action();
    return {};
}

void SyncpointManager::DeregisterAction(std::list<RegisteredAction>& action_storage,
                                        const ActionHandle& handle) {
    if (handle == 0) {
        return;
    }
    // A captured object's destructor may call back into this manager too.
    std::function<void()> retired;
    std::unique_lock lk(guard);
    const auto matches = [handle](const RegisteredAction& action) { return action.id == handle; };
    if (const auto it = std::ranges::find_if(action_storage, matches); it != action_storage.end()) {
        retired = std::move(it->action);
        action_storage.erase(it);
        return;
    }
    for (;;) {
        const DueActions* running_in{};
        for (DueActions* due = due_actions; due; due = due->next) {
            if (const auto it = std::ranges::find_if(due->pending, matches);
                it != due->pending.end()) {
                retired = std::move(it->action);
                due->pending.erase(it);
                return;
            }
            if (due->running == handle) {
                running_in = due;
            }
        }
        if (!running_in || running_in->thread == std::this_thread::get_id()) {
            return;
        }
        action_done_cv.wait(lk);
    }
}

void SyncpointManager::DeregisterGuestAction(u32 syncpoint_id, const ActionHandle& handle) {
    DeregisterAction(guest_action_storage[syncpoint_id], handle);
}

void SyncpointManager::DeregisterHostAction(u32 syncpoint_id, const ActionHandle& handle) {
    DeregisterAction(host_action_storage[syncpoint_id], handle);
}

void SyncpointManager::IncrementGuest(u32 syncpoint_id) {
    if (syncpoint_id >= NUM_MAX_SYNCPOINTS) {
        LOG_ERROR(HW_GPU, "Ignoring increment of invalid syncpoint {:#x}", syncpoint_id);
        return;
    }
    Increment(syncpoints_guest[syncpoint_id], wait_guest_cv, guest_action_storage[syncpoint_id],
              syncpoint_id);
}

void SyncpointManager::SetGuestMirror(GuestMirror mirror, const void* owner) {
    std::scoped_lock lk(guard);
    guest_mirror = std::move(mirror);
    guest_mirror_owner = owner;
}

void SyncpointManager::ClearGuestMirror(const void* owner) {
    std::scoped_lock lk(guard);
    if (guest_mirror_owner == owner) {
        guest_mirror = {};
        guest_mirror_owner = nullptr;
    }
}

void SyncpointManager::IncrementHost(u32 syncpoint_id) {
    if (syncpoint_id >= NUM_MAX_SYNCPOINTS) {
        return;
    }
    Increment(syncpoints_host[syncpoint_id], wait_host_cv, host_action_storage[syncpoint_id]);
}

void SyncpointManager::WaitGuest(u32 syncpoint_id, u32 expected_value) {
    Wait(syncpoints_guest[syncpoint_id], wait_guest_cv, expected_value);
}

void SyncpointManager::WaitHost(u32 syncpoint_id, u32 expected_value) {
    Wait(syncpoints_host[syncpoint_id], wait_host_cv, expected_value);
}

bool SyncpointManager::WaitHost(u32 syncpoint_id, u32 expected_value,
                                const std::stop_token& stop_token) {
    if (syncpoint_id >= syncpoints_host.size()) {
        LOG_ERROR(HW_GPU, "Invalid host syncpoint {} in command wait", syncpoint_id);
        return false;
    }
    const auto reached = [&] { return IsReadyHost(syncpoint_id, expected_value); };
    if (reached()) {
        return true;
    }
    // Take the same mutex as the waiter so cancellation cannot notify between the stop check
    // and sleeping. Register before locking because an already-stopped token calls us inline.
    const std::stop_callback wake{stop_token, [this] {
        std::scoped_lock lk(guard);
        wait_host_cv.notify_all();
    }};
    std::unique_lock lk(guard);
    wait_host_cv.wait(lk, [&] { return reached() || stop_token.stop_requested(); });
    return reached();
}

void SyncpointManager::Increment(std::atomic<u32>& syncpoint, std::condition_variable& wait_cv,
                                 std::list<RegisteredAction>& action_storage,
                                 std::optional<u32> mirrored_id) {
    std::unique_lock lk(guard);
    if (mirrored_id && guest_mirror) {
        // Before the value is published: the lock-free ready checks in RegisterAction and Wait
        // must not see it ahead of the page.
        guest_mirror(*mirrored_id, syncpoint.load(std::memory_order_relaxed) + 1);
    }
    const u32 new_value{syncpoint.fetch_add(1, std::memory_order_acq_rel) + 1};
    wait_cv.notify_all();
    auto end = action_storage.begin();
    while (end != action_storage.end() && end->expected_value <= new_value) {
        ++end;
    }
    if (end == action_storage.begin()) {
        return;
    }
    DueActions due;
    due.pending.splice(due.pending.end(), action_storage, action_storage.begin(), end);
    due.next = due_actions;
    due_actions = &due;
    SCOPE_EXIT {
        if (!lk.owns_lock()) {
            lk.lock();
        }
        auto** link = &due_actions;
        while (*link != &due) {
            link = &(*link)->next;
        }
        *link = due.next;
        action_done_cv.notify_all();
    };
    // A composite action may block on presentation. Other syncpoints and cancellation must
    // progress while it runs. Preserve this batch's order even if an action throws.
    std::exception_ptr first_failure;
    while (!due.pending.empty()) {
        std::function<void()> action{std::move(due.pending.front().action)};
        due.running = due.pending.front().id;
        due.pending.pop_front();
        lk.unlock();
        try {
            action();
        } catch (...) {
            if (!first_failure) {
                first_failure = std::current_exception();
            }
        }
        action = nullptr;
        lk.lock();
        due.running = 0;
        action_done_cv.notify_all();
    }
    if (first_failure) {
        std::rethrow_exception(first_failure);
    }
}

void SyncpointManager::Wait(std::atomic<u32>& syncpoint, std::condition_variable& wait_cv,
                            u32 expected_value) {
    const auto pred = [&]() { return syncpoint.load(std::memory_order_acquire) >= expected_value; };
    if (pred()) {
        return;
    }

    std::unique_lock lk(guard);
    wait_cv.wait(lk, pred);
}

} // namespace Host1x

} // namespace Tegra
