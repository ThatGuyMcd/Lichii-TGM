// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "video_core/host1x/syncpoint_manager.h"

namespace {
using Manager = Tegra::Host1x::SyncpointManager;
using namespace std::chrono_literals;

// Timed observations always release the blocked callback before joining the threads, including
// on a failed check, so the old lock-held implementation reports a failure instead of hanging.
struct BlockedAction {
    std::promise<void> entered;
    std::promise<void> release;
    std::shared_future<void> gate{release.get_future().share()};
    void Run() {
        entered.set_value();
        gate.wait();
    }
};
} // namespace

TEST_CASE("Native60: a blocked syncpoint callback does not hold the manager guard",
          "[video_core][host1x][native60]") {
    Manager manager;
    BlockedAction blocked;
    auto entered = blocked.entered.get_future();
    manager.RegisterGuestAction(1, 1, [&] { blocked.Run(); });
    auto increment = std::async(std::launch::async, [&] { manager.IncrementGuest(1); });
    const bool started = entered.wait_for(2s) == std::future_status::ready;
    CHECK(started);
    std::atomic<u32> other_calls{};
    auto independent = std::async(std::launch::async, [&] {
        manager.RegisterHostAction(2, 1, [&] { ++other_calls; });
        manager.IncrementHost(2);
        // This would also take the guard even after the ready value is published.
        manager.WithGuestValuesStable([] {});
    });
    const bool progressed = independent.wait_for(500ms) == std::future_status::ready;
    CHECK(progressed);
    CHECK(manager.GetGuestSyncpointValue(1) == 1);
    blocked.release.set_value();
    increment.get();
    independent.get();
    CHECK(other_calls == 1);
}

TEST_CASE("Native60: cancelling an action removed into a due batch prevents its callback",
          "[video_core][host1x][native60]") {
    Manager manager;
    BlockedAction blocked;
    auto entered = blocked.entered.get_future();
    std::atomic<u32> cancelled_calls{};
    // Equal values retain the existing reverse registration order: blocker goes first.
    const auto target = manager.RegisterGuestAction(3, 1, [&] { ++cancelled_calls; });
    manager.RegisterGuestAction(3, 1, [&] { blocked.Run(); });
    auto increment = std::async(std::launch::async, [&] { manager.IncrementGuest(3); });
    CHECK(entered.wait_for(2s) == std::future_status::ready);
    auto cancel = std::async(std::launch::async, [&] { manager.DeregisterGuestAction(3, target); });
    const bool cancelled = cancel.wait_for(500ms) == std::future_status::ready;
    CHECK(cancelled);
    blocked.release.set_value();
    increment.get();
    cancel.get();
    CHECK(cancelled_calls == 0);
}

TEST_CASE("Native60: cancellation joins an executing action and its capture destruction",
          "[video_core][host1x][native60]") {
    Manager manager;
    BlockedAction blocked;
    auto entered = blocked.entered.get_future();
    std::atomic<bool> destroyed{};
    struct Capture {
        std::atomic<bool>& destroyed;
        ~Capture() { destroyed.store(true, std::memory_order_release); }
    };
    auto capture = std::make_shared<Capture>(destroyed);
    const auto target = manager.RegisterHostAction(4, 1, [&, capture] { blocked.Run(); });
    capture.reset();
    auto increment = std::async(std::launch::async, [&] { manager.IncrementHost(4); });
    CHECK(entered.wait_for(2s) == std::future_status::ready);
    std::promise<void> cancelling;
    auto beginning = cancelling.get_future();
    auto cancel = std::async(std::launch::async, [&] {
        cancelling.set_value();
        manager.DeregisterHostAction(4, target);
        return destroyed.load(std::memory_order_acquire);
    });
    beginning.wait();
    CHECK(cancel.wait_for(30ms) == std::future_status::timeout);
    CHECK_FALSE(destroyed.load(std::memory_order_acquire));
    blocked.release.set_value();
    increment.get();
    CHECK(cancel.get());
}

TEST_CASE("Native60: actions can cancel themselves and reenter the manager",
          "[video_core][host1x][native60]") {
    Manager manager;
    Manager::ActionHandle self{};
    u32 called{};
    self = manager.RegisterGuestAction(5, 1, [&] {
        manager.DeregisterGuestAction(5, self);
        manager.WithGuestValuesStable([&] { ++called; });
        CHECK(manager.RegisterGuestAction(5, 1, [&] { ++called; }) == 0);
        manager.RegisterHostAction(6, 1, [&] { ++called; });
        manager.IncrementHost(6);
    });
    manager.IncrementGuest(5);
    CHECK(called == 3);
    manager.DeregisterGuestAction(5, self);
    manager.IncrementGuest(5);
    CHECK(called == 3);
}

TEST_CASE("Native60: an action exception drains its batch and releases cancellation joins",
          "[video_core][host1x][native60]") {
    Manager manager;
    std::vector<u32> order;
    const auto last = manager.RegisterGuestAction(7, 1, [&] { order.push_back(2); });
    const auto first = manager.RegisterGuestAction(7, 1, [&] {
        order.push_back(1);
        manager.IncrementHost(8);
        throw std::runtime_error{"action failure"};
    });
    REQUIRE_THROWS_AS(manager.IncrementGuest(7), std::runtime_error);
    CHECK(order == std::vector<u32>{1, 2});
    CHECK(manager.GetHostSyncpointValue(8) == 1);
    manager.DeregisterGuestAction(7, first);
    manager.DeregisterGuestAction(7, last);
    manager.RegisterGuestAction(7, 2, [&] { order.push_back(3); });
    manager.IncrementGuest(7);
    CHECK(order == std::vector<u32>{1, 2, 3});
}

TEST_CASE("Native60: cancelled capture destruction can reenter the manager",
          "[video_core][host1x][native60]") {
    Manager manager;
    bool destroyed{};
    struct Capture {
        Manager& manager;
        bool& destroyed;
        ~Capture() {
            manager.WithGuestValuesStable([&] { destroyed = true; });
        }
    };
    auto capture = std::make_shared<Capture>(manager, destroyed);
    const auto action = manager.RegisterHostAction(9, 1, [capture] {});
    capture.reset();
    manager.DeregisterHostAction(9, action);
    CHECK(destroyed);
    manager.IncrementHost(9);
}

TEST_CASE("Native60: guest mirroring remains before publication and unlocked actions",
          "[video_core][host1x][native60]") {
    Manager manager;
    u32 mirrored{};
    int owner;
    manager.SetGuestMirror([&](u32 id, u32 value) {
        CHECK(id == 10);
        CHECK(manager.GetGuestSyncpointValue(id) + 1 == value);
        mirrored = value;
    }, &owner);
    manager.RegisterGuestAction(10, 1, [&] {
        CHECK(mirrored == 1);
        CHECK(manager.GetGuestSyncpointValue(10) == 1);
        manager.ClearGuestMirror(&owner);
    });
    manager.IncrementGuest(10);
    manager.IncrementGuest(10);
    CHECK(mirrored == 1);
}
