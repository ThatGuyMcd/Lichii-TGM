// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <cstdlib>
#include <future>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "video_core/host1x/syncpoint_manager.h"

using Tegra::Host1x::SyncpointManager;

TEST_CASE("Already reached syncpoints run actions immediately with a zero handle",
          "[video_core][syncpoint_actions]") {
    SyncpointManager manager;
    u32 guest_calls{};
    u32 host_calls{};
    for (u32 value = 0; value < 3; ++value) {
        const auto guest = manager.RegisterGuestAction(0, value, [&] { ++guest_calls; });
        const auto host = manager.RegisterHostAction(0, value, [&] { ++host_calls; });
        REQUIRE(guest == 0);
        REQUIRE(host == 0);
        REQUIRE(guest_calls == value + 1);
        REQUIRE(host_calls == value + 1);
        manager.DeregisterGuestAction(0, guest);
        manager.DeregisterHostAction(0, host);
        manager.IncrementGuest(0);
        manager.IncrementHost(0);
        REQUIRE(guest_calls == value + 1);
        REQUIRE(host_calls == value + 1);
    }
}

TEST_CASE("Cancelling pending syncpoint actions prevents their callbacks",
          "[video_core][syncpoint_actions]") {
    SyncpointManager manager;
    u32 calls{};
    const auto guest = manager.RegisterGuestAction(0, 1, [&] { ++calls; });
    const auto host = manager.RegisterHostAction(0, 1, [&] { ++calls; });
    REQUIRE(guest != 0);
    REQUIRE(host != 0);
    REQUIRE(guest != host);
    manager.DeregisterGuestAction(0, guest);
    manager.DeregisterHostAction(0, host);
    manager.DeregisterGuestAction(0, guest);
    manager.DeregisterHostAction(0, host);
    manager.IncrementGuest(0);
    manager.IncrementHost(0);
    REQUIRE(calls == 0);
    REQUIRE(manager.GetGuestSyncpointValue(0) == 1);
    REQUIRE(manager.GetHostSyncpointValue(0) == 1);
}

TEST_CASE("Cancelling completed syncpoint actions is harmless",
          "[video_core][syncpoint_actions]") {
    SyncpointManager manager;
    u32 guest_calls{};
    u32 host_calls{};
    const auto guest = manager.RegisterGuestAction(0, 1, [&] { ++guest_calls; });
    const auto host = manager.RegisterHostAction(0, 1, [&] { ++host_calls; });
    manager.IncrementGuest(0);
    manager.IncrementHost(0);
    REQUIRE(guest_calls == 1);
    REQUIRE(host_calls == 1);
    for (u32 repeat = 0; repeat < 3; ++repeat) {
        manager.DeregisterGuestAction(0, guest);
        manager.DeregisterHostAction(0, host);
        manager.IncrementGuest(0);
        manager.IncrementHost(0);
    }
    REQUIRE(guest_calls == 1);
    REQUIRE(host_calls == 1);
}

TEST_CASE("Stale syncpoint handles cannot cancel replacement actions",
          "[video_core][syncpoint_actions]") {
    SyncpointManager manager;
    u32 guest_calls{};
    u32 host_calls{};
    SyncpointManager::ActionHandle previous_guest{};
    SyncpointManager::ActionHandle previous_host{};
    for (u32 target = 1; target <= 256; ++target) {
        const auto guest = manager.RegisterGuestAction(0, target, [&] { ++guest_calls; });
        const auto host = manager.RegisterHostAction(0, target, [&] { ++host_calls; });
        REQUIRE(guest > previous_guest);
        REQUIRE(host > previous_host);
        REQUIRE(guest != host);
        manager.DeregisterGuestAction(0, previous_guest);
        manager.DeregisterHostAction(0, previous_host);
        manager.IncrementGuest(0);
        manager.IncrementHost(0);
        REQUIRE(guest_calls == target);
        REQUIRE(host_calls == target);
        previous_guest = guest;
        previous_host = host;
    }
}

TEST_CASE("Syncpoint action IDs stay distinct across counters and host guest banks",
          "[video_core][syncpoint_actions]") {
    SyncpointManager manager;
    const auto stale = manager.RegisterGuestAction(0, 1, [] {});
    manager.IncrementGuest(0);
    u32 guest_calls{};
    u32 host_calls{};
    const auto guest = manager.RegisterGuestAction(1, 1, [&] { ++guest_calls; });
    const auto host = manager.RegisterHostAction(1, 1, [&] { ++host_calls; });
    REQUIRE(stale != guest);
    REQUIRE(stale != host);
    REQUIRE(guest != host);
    manager.DeregisterGuestAction(1, stale);
    manager.DeregisterHostAction(1, stale);
    manager.DeregisterGuestAction(1, host);
    manager.DeregisterHostAction(1, guest);
    manager.DeregisterGuestAction(0, guest);
    manager.DeregisterHostAction(0, host);
    manager.IncrementGuest(1);
    manager.IncrementHost(1);
    REQUIRE(guest_calls == 1);
    REQUIRE(host_calls == 1);
}

TEST_CASE("The guest mirror sees each increment before it is published and its actions run",
          "[video_core][syncpoint_actions]") {
    SyncpointManager manager;
    std::vector<u32> mirrored;
    bool published_early{};
    manager.SetGuestMirror([&](u32 id, u32 value) {
        REQUIRE(id == 3);
        published_early |= manager.IsReadyGuest(id, value);
        mirrored.push_back(value);
    });
    u32 mirrored_at_action{};
    manager.RegisterGuestAction(3, 2, [&] { mirrored_at_action = mirrored.back(); });
    manager.IncrementGuest(3);
    manager.IncrementGuest(3);
    manager.IncrementHost(3);
    REQUIRE(mirrored == std::vector<u32>{1, 2});
    REQUIRE_FALSE(published_early);
    REQUIRE(mirrored_at_action == 2);

    manager.SetGuestMirror({});
    manager.IncrementGuest(3);
    REQUIRE(mirrored.size() == 2);
    REQUIRE(manager.GetGuestSyncpointValue(3) == 3);
}

TEST_CASE("Guest increments wait for a stable copy of the guest values",
          "[video_core][syncpoint_actions]") {
    SyncpointManager manager;
    std::thread increment;
    u32 value_while_stable{};
    manager.WithGuestValuesStable([&] {
        increment = std::thread([&] { manager.IncrementGuest(0); });
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        value_while_stable = manager.GetGuestSyncpointValue(0);
    });
    increment.join();
    REQUIRE(value_while_stable == 0);
    REQUIRE(manager.GetGuestSyncpointValue(0) == 1);
}

TEST_CASE("An older syncpoint mirror owner cannot clear its replacement",
          "[video_core][syncpoint_actions]") {
    SyncpointManager manager;
    const int first{}, second{};
    u32 first_calls{}, second_calls{};
    manager.SetGuestMirror([&](u32, u32) { ++first_calls; }, &first);
    manager.IncrementGuest(1);
    manager.SetGuestMirror([&](u32, u32) { ++second_calls; }, &second);
    manager.ClearGuestMirror(&first);
    manager.IncrementGuest(1);
    REQUIRE(first_calls == 1);
    REQUIRE(second_calls == 1);
    manager.ClearGuestMirror(&second);
    manager.IncrementGuest(1);
    REQUIRE(second_calls == 1);
}

TEST_CASE("Retiring a running host action waits for its callback",
          "[video_core][syncpoint_actions][host_syncpoint_retirement]") {
    using namespace std::chrono_literals;
    SyncpointManager manager;
    std::promise<void> callback_entered;
    auto entered = callback_entered.get_future();
    std::promise<void> unblock_callback;
    auto unblocked = unblock_callback.get_future();
    const auto handle = manager.RegisterHostAction(5, 1, [&] {
        callback_entered.set_value();
        unblocked.wait();
    });
    std::jthread incrementer{[&] { manager.IncrementHost(5); }};
    const bool entered_on_time = entered.wait_for(3s) == std::future_status::ready;
    std::promise<void> retirement_started;
    auto started = retirement_started.get_future();
    auto retirement = std::async(std::launch::async, [&] {
        retirement_started.set_value();
        manager.DeregisterHostAction(5, handle);
    });
    const bool started_on_time = started.wait_for(3s) == std::future_status::ready;
    const bool returned_early = retirement.wait_for(50ms) == std::future_status::ready;
    unblock_callback.set_value();
    if (retirement.wait_for(3s) != std::future_status::ready) {
        std::abort();
    }
    retirement.get();
    incrementer.join();
    REQUIRE(entered_on_time);
    REQUIRE(started_on_time);
    CHECK_FALSE(returned_early);
}

TEST_CASE("Retiring a completed host action does not wait for a different callback",
          "[video_core][syncpoint_actions][host_syncpoint_retirement]") {
    using namespace std::chrono_literals;
    SyncpointManager manager;
    std::promise<void> callback_entered;
    auto entered = callback_entered.get_future();
    std::promise<void> unblock_callback;
    auto unblocked = unblock_callback.get_future();
    manager.RegisterHostAction(6, 1, [&] {
        callback_entered.set_value();
        unblocked.wait();
    });
    // Equal-value actions run newest first.
    const auto completed = manager.RegisterHostAction(6, 1, [] {});
    std::jthread incrementer{[&] { manager.IncrementHost(6); }};
    const bool entered_on_time = entered.wait_for(3s) == std::future_status::ready;
    auto retirement = std::async(std::launch::async, [&] {
        manager.DeregisterHostAction(6, completed);
    });
    const bool returned_while_blocked = retirement.wait_for(3s) == std::future_status::ready;
    unblock_callback.set_value();
    if (retirement.wait_for(3s) != std::future_status::ready) {
        std::abort();
    }
    retirement.get();
    incrementer.join();
    REQUIRE(entered_on_time);
    CHECK(returned_while_blocked);
}
