// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <memory>
#include <new>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/common_types.h"
#include "common/scope_exit.h"
#include "video_core/renderer_vulkan/retirement_ownership.h"

namespace {
bool fail_retirement_allocation{};

template <typename T>
struct RetirementAllocator : std::allocator<T> {
    using value_type = T;
    RetirementAllocator() = default;
    template <typename U>
    RetirementAllocator(const RetirementAllocator<U>&) noexcept {}
    T* allocate(size_t count) {
        if (fail_retirement_allocation) {
            throw std::bad_alloc{};
        }
        return std::allocator<T>::allocate(count);
    }
    template <typename U>
    bool operator==(const RetirementAllocator<U>&) const noexcept { return true; }
};

struct Sentinel {
    explicit Sentinel(size_t& count_) : count{count_} {}
    ~Sentinel() { ++count; }
    size_t& count;
};

struct Resources {
    std::unique_ptr<Sentinel> sparse;
    std::unique_ptr<Sentinel> gathered;
    u64 tick{};
};
using Retired = std::vector<Resources, RetirementAllocator<Resources>>;

Resources MakeResources(size_t& sparse, size_t& gathered, u64 tick) {
    return {std::make_unique<Sentinel>(sparse), std::make_unique<Sentinel>(gathered), tick};
}
} // namespace

TEST_CASE("Multi-range retirement reserves before transferring live resource ownership",
          "[video_core][oot_perf][retirement_ownership]") {
    fail_retirement_allocation = false;
    SCOPE_EXIT { fail_retirement_allocation = false; };
    std::array<size_t, 4> destroyed{};
    Retired retired;
    retired.reserve(1);
    auto first = MakeResources(destroyed[0], destroyed[1], 3);
    u64 completed{};
    const auto is_complete = [&](u64 tick) { return tick <= completed; };
    Vulkan::RetireOwnership(retired, is_complete, [&]() noexcept { return std::move(first); });
    REQUIRE_FALSE(first.sparse);
    REQUIRE_FALSE(first.gathered);
    auto next = MakeResources(destroyed[2], destroyed[3], 8);
    auto* const sparse = next.sparse.get();
    auto* const gathered = next.gathered.get();
    size_t transfers{};
    const auto transfer = [&]() noexcept {
        ++transfers;
        return std::move(next);
    };
    fail_retirement_allocation = true;
    REQUIRE_THROWS_AS(Vulkan::RetireOwnership(retired, is_complete, transfer), std::bad_alloc);
    REQUIRE(transfers == 0);
    REQUIRE(next.sparse.get() == sparse);
    REQUIRE(next.gathered.get() == gathered);
    REQUIRE(retired.size() == 1);
    REQUIRE(destroyed == std::array<size_t, 4>{});

    fail_retirement_allocation = false;
    Vulkan::RetireOwnership(retired, is_complete, transfer);
    REQUIRE(transfers == 1);
    REQUIRE_FALSE(next.sparse);
    REQUIRE_FALSE(next.gathered);
    REQUIRE(retired.size() == 2);
    REQUIRE(destroyed == std::array<size_t, 4>{});
    // The newly recorded tick is deliberately incomplete. Neither collection nor a failed
    // capacity acquisition may release its resources or pretend to advance GPU completion.
    fail_retirement_allocation = true;
    Vulkan::DrainRetiredOwnership(retired, is_complete);
    REQUIRE(completed == 0);
    REQUIRE(retired.size() == 2);
    REQUIRE(destroyed == std::array<size_t, 4>{});
    completed = 3;
    Vulkan::DrainRetiredOwnership(retired, is_complete);
    REQUIRE(retired.size() == 1);
    REQUIRE(destroyed == std::array<size_t, 4>{1, 1, 0, 0});
    REQUIRE(retired.front().sparse.get() == sparse);
    REQUIRE(retired.front().gathered.get() == gathered);
    completed = 7;
    Vulkan::DrainRetiredOwnership(retired, is_complete);
    REQUIRE(retired.size() == 1);
    REQUIRE(destroyed == std::array<size_t, 4>{1, 1, 0, 0});
    completed = 8;
    Vulkan::DrainRetiredOwnership(retired, is_complete);
    REQUIRE(retired.empty());
    REQUIRE(destroyed == std::array<size_t, 4>{1, 1, 1, 1});
    Vulkan::DrainRetiredOwnership(retired, is_complete);
    REQUIRE(destroyed == std::array<size_t, 4>{1, 1, 1, 1});
}

TEST_CASE("Completed multi-range retirements make room without allocation or premature transfer",
          "[video_core][oot_perf][retirement_ownership]") {
    fail_retirement_allocation = false;
    SCOPE_EXIT { fail_retirement_allocation = false; };
    std::array<size_t, 4> destroyed{};
    Retired retired;
    retired.reserve(1);
    auto first = MakeResources(destroyed[0], destroyed[1], 2);
    u64 completed{};
    const auto is_complete = [&](u64 tick) { return tick <= completed; };
    Vulkan::RetireOwnership(retired, is_complete, [&]() noexcept { return std::move(first); });
    auto next = MakeResources(destroyed[2], destroyed[3], 9);
    completed = 2;
    fail_retirement_allocation = true;
    Vulkan::RetireOwnership(retired, is_complete, [&]() noexcept { return std::move(next); });
    REQUIRE(retired.size() == 1);
    REQUIRE(retired.front().tick == 9);
    REQUIRE_FALSE(next.sparse);
    REQUIRE_FALSE(next.gathered);
    REQUIRE(destroyed == std::array<size_t, 4>{1, 1, 0, 0});
    completed = 9;
    Vulkan::DrainRetiredOwnership(retired, is_complete);
    REQUIRE(destroyed == std::array<size_t, 4>{1, 1, 1, 1});
}
