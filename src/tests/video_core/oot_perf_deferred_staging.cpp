// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <new>
#include <set>
#include <catch2/catch_test_macros.hpp>
#include "common/scope_exit.h"
#include "video_core/renderer_vulkan/deferred_buffer_cache.h"

namespace {
bool fail_allocations{};
size_t allocations{};

template <typename T>
struct CheckedAllocator : std::allocator<T> {
    using value_type = T;
    CheckedAllocator() = default;
    template <typename U>
    CheckedAllocator(const CheckedAllocator<U>&) noexcept {}
    T* allocate(size_t n) {
        if (fail_allocations) {
            throw std::bad_alloc{};
        }
        ++allocations;
        return std::allocator<T>::allocate(n);
    }
    template <typename U>
    bool operator==(const CheckedAllocator<U>&) const noexcept { return true; }
};

struct Buffer {
    u64 index{};
    std::unique_ptr<u32> payload;
};
using Cache = Vulkan::DeferredBufferCache<Buffer, CheckedAllocator>;
Buffer MakeBuffer(u64 index) { return {index, std::make_unique<u32>(u32(index + 10))}; }
} // namespace

TEST_CASE("Deferred staging ownership survives allocation failures and returns without allocation",
          "[video_core][oot_perf][deferred_staging]") {
    fail_allocations = false;
    SCOPE_EXIT { fail_allocations = false; };
    Cache cache;
    cache.Add(MakeBuffer(1), false);
    cache.Add(MakeBuffer(2), false);
    auto* payload = cache.Entries()[0].payload.get();
    fail_allocations = true;
    REQUIRE_THROWS_AS(cache.Hold(0), std::bad_alloc);
    REQUIRE(cache.Size() == 2);
    REQUIRE(cache.Entries().size() == 2);
    REQUIRE(cache.Entries()[0].payload.get() == payload);
    REQUIRE(cache.FindHeld(1) == nullptr);

    fail_allocations = false;
    auto& held = cache.Hold(0);
    REQUIRE(held.payload.get() == payload);
    REQUIRE(cache.Entries().size() == 1);
    REQUIRE(cache.Entries()[0].index == 2);
    REQUIRE(cache.FindHeld(1) == &held);
    // Create and retire other buffers while this download is held. Every held buffer retains
    // a reserved return slot even when the active vector grows or GC removes its entries.
    for (u64 i = 3; i < 100; ++i) {
        cache.Add(MakeBuffer(i), i % 2 == 0);
    }
    cache.Entries().clear();
    const size_t before = allocations;
    fail_allocations = true;
    auto& returned = cache.Release(1);
    REQUIRE(returned.payload.get() == payload);
    REQUIRE(*returned.payload == 11);
    REQUIRE(cache.FindHeld(1) == nullptr);
    for (u64 i = 4; i < 100; i += 2) {
        REQUIRE(cache.Release(i).index == i);
    }
    REQUIRE(allocations == before);
    REQUIRE(cache.Size() == 49);
    REQUIRE(cache.Entries().size() == 49);
}

TEST_CASE("Failed staging pool growth preserves outstanding download ownership",
          "[video_core][oot_perf][deferred_staging]") {
    fail_allocations = false;
    SCOPE_EXIT { fail_allocations = false; };
    Cache cache;
    auto& first = cache.Add(MakeBuffer(1), true);
    auto* payload = first.payload.get();
    fail_allocations = true;
    REQUIRE_THROWS_AS(cache.Add(MakeBuffer(2), false), std::bad_alloc);
    REQUIRE(cache.Size() == 1);
    REQUIRE(cache.FindHeld(1)->payload.get() == payload);
    REQUIRE(cache.Release(1).payload.get() == payload);
}

TEST_CASE("Deferred staging shuffled checkout keeps swapped candidates and every owner",
          "[video_core][oot_perf][deferred_staging]") {
    Cache cache;
    for (u64 i = 0; i < 64; ++i) {
        cache.Add(MakeBuffer(i), false);
    }
    for (size_t round = 0; round < 8; ++round) {
        std::set<u64> held;
        while (!cache.Entries().empty()) {
            const size_t position = (round * 13 + cache.Entries().size() / 3) % cache.Entries().size();
            const u64 last = cache.Entries().back().index;
            const u64 picked = cache.Entries()[position].index;
            REQUIRE(held.insert(cache.Hold(position).index).second);
            REQUIRE(cache.FindHeld(picked)->payload != nullptr);
            if (position < cache.Entries().size()) {
                // The pool resumes its reuse scan at this position after checkout.
                REQUIRE(cache.Entries()[position].index == last);
            }
        }
        REQUIRE(held.size() == 64);
        for (auto it = held.rbegin(); it != held.rend(); ++it) {
            REQUIRE(*cache.Release(*it).payload == *it + 10);
        }
        REQUIRE(cache.Size() == 64);
    }
}
