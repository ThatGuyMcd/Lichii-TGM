// SPDX-License-Identifier: GPL-3.0-or-later
#include <chrono>
#include <catch2/catch_test_macros.hpp>
#include "core/core_timing.h"

namespace {
using namespace std::chrono_literals;

void CheckExpiredOwner(bool looping) {
    Core::Timing::CoreTiming timing;
    timing.SetMulticore(false);
    timing.Initialize([] {});
    unsigned expired_calls{}, live_calls{}, future_calls{};
    auto expired = Core::Timing::CreateEvent("synthetic expired", [&](s64, auto) {
        ++expired_calls;
        return std::optional<std::chrono::nanoseconds>{};
    });
    if (looping) timing.ScheduleLoopingEvent(0ns, 1ms, expired);
    else timing.ScheduleEvent(0ns, expired);
    expired.reset();
    auto live = Core::Timing::CreateEvent("synthetic live", [&](s64, auto) {
        ++live_calls;
        return std::optional<std::chrono::nanoseconds>{};
    });
    auto future = Core::Timing::CreateEvent("synthetic future", [&](s64, auto) {
        ++future_calls;
        return std::optional<std::chrono::nanoseconds>{};
    });
    timing.ScheduleEvent(0ns, live);
    timing.ScheduleEvent(10ms, future);
    const auto deadline = timing.Advance();
    CHECK(expired_calls == 0);
    CHECK(live_calls == 1);
    CHECK(future_calls == 0);
    CHECK(deadline == std::chrono::duration_cast<std::chrono::nanoseconds>(10ms).count());
    timing.AddTicks(1'000'000'000);
    CHECK_FALSE(timing.Advance().has_value());
    CHECK(expired_calls == 0);
    CHECK(live_calls == 1);
    CHECK(future_calls == 1);
}
}

TEST_CASE("Expired one-shot timing owners cannot block the next live deadline", "[.][expired_timing_event]") {
    CheckExpiredOwner(false);
}
TEST_CASE("Expired repeating timing owners cannot block the next live deadline", "[.][expired_timing_event]") {
    CheckExpiredOwner(true);
}
TEST_CASE("Live repeating timing events keep their next deadline and cancellation", "[.][expired_timing_event]") {
    Core::Timing::CoreTiming timing;
    timing.SetMulticore(false);
    timing.Initialize([] {});
    unsigned calls{};
    auto event = Core::Timing::CreateEvent("synthetic repeating", [&](s64, auto) {
        ++calls;
        return std::optional<std::chrono::nanoseconds>{};
    });
    timing.ScheduleLoopingEvent(0ns, 2ms, event);
    const auto next = std::chrono::duration_cast<std::chrono::nanoseconds>(2ms).count();
    CHECK(timing.Advance() == next);
    CHECK(calls == 1);
    CHECK(timing.Advance() == next);
    CHECK(calls == 1);
    timing.UnscheduleEvent(event);
    CHECK_FALSE(timing.Advance().has_value());
    CHECK(calls == 1);
}
