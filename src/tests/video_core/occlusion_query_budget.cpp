// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <memory>

#include "video_core/renderer_vulkan/vk_query_budget.h"
#include "video_core/renderer_vulkan/query_pool_reset.h"

TEST_CASE("Occlusion query windows retain the Water Temple counts within a bounded bank budget",
          "[video_core][query_cache]") {
    using namespace Vulkan::OcclusionQueryBudget;

    STATIC_REQUIRE(MaxRunningSlots == 2048);
    REQUIRE_FALSE(ShouldCarry(0));
    REQUIRE_FALSE(ShouldCarry(346));
    REQUIRE_FALSE(ShouldCarry(1024));
    REQUIRE_FALSE(ShouldCarry(1025));
    REQUIRE_FALSE(ShouldCarry(1027));
    REQUIRE_FALSE(ShouldCarry(1035));
    REQUIRE_FALSE(ShouldCarry(1058));
    REQUIRE_FALSE(ShouldCarry(MaxRunningSlots));
    REQUIRE(ShouldCarry(MaxRunningSlots + 1));
    REQUIRE(ShouldCarry(16384));
}

TEST_CASE("A queued query reset owns its pool until execution or queue disposal",
          "[video_core][query_cache]") {
    struct Pool {
        int resets{};
    };
    auto pool = std::make_shared<Pool>();
    std::weak_ptr<Pool> lifetime = pool;
    std::function<void(int)> queued = Vulkan::MakeQueryPoolReset(pool, [](Pool& retained, int count) {
        retained.resets += count;
    });
    pool.reset();
    REQUIRE_FALSE(lifetime.expired());
    queued(1);
    REQUIRE(lifetime.lock()->resets == 1);
    queued = {};
    REQUIRE(lifetime.expired());

    pool = std::make_shared<Pool>();
    lifetime = pool;
    queued = Vulkan::MakeQueryPoolReset(pool, [](Pool&, int) { FAIL("Discarded reset executed"); });
    pool.reset();
    REQUIRE_FALSE(lifetime.expired());
    queued = {};
    REQUIRE(lifetime.expired());
}
