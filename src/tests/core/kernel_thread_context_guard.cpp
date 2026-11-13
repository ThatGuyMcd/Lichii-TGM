// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <thread>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "core/hle/kernel/k_thread_context_guard.h"

TEST_CASE("Thread context guard rejects a repeated try and can be acquired again", "[kernel][context_guard]") {
    Kernel::KThreadContextGuard guard;
    REQUIRE(guard.try_lock());
    CHECK_FALSE(guard.try_lock());
    guard.unlock();
    CHECK(guard.try_lock());
    guard.unlock();
}

TEST_CASE("Thread context guard serializes saved context writes across host threads", "[kernel][context_guard]") {
    Kernel::KThreadContextGuard guard;
    unsigned saved_context{};
    std::vector<std::jthread> workers;
    for (unsigned thread = 0; thread < 8; ++thread) {
        workers.emplace_back([&] {
            for (unsigned switch_count = 0; switch_count < 10000; ++switch_count) {
                while (!guard.try_lock()) std::this_thread::yield();
                ++saved_context;
                guard.unlock();
            }
        });
    }
    workers.clear();
    CHECK(saved_context == 80000);
}

TEST_CASE("Thread context ownership can be handed across a fiber migration", "[kernel][context_guard]") {
    Kernel::KThreadContextGuard guard;
    std::atomic<bool> ready{};
    unsigned saved_context{};
    std::jthread old_core([&] {
        while (!guard.try_lock()) std::this_thread::yield();
        saved_context = 0x1234;
        ready.store(true, std::memory_order_release);
    });
    while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
    old_core.join();
    CHECK_FALSE(guard.try_lock());
    guard.unlock();
    std::jthread new_core([&] {
        while (!guard.try_lock()) std::this_thread::yield();
        saved_context += 1;
        guard.unlock();
    });
    new_core.join();
    CHECK(saved_context == 0x1235);
}
