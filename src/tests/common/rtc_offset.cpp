// SPDX-License-Identifier: GPL-3.0-or-later
#include <limits>
#include <catch2/catch_test_macros.hpp>
#include "common/rtc_offset.h"

TEST_CASE("Custom RTC preserves signed offsets and saturates the full setting range", "[rtc]") {
    REQUIRE(Common::ApplyRtcOffset(1000, -250) == 750);
    REQUIRE(Common::ApplyRtcOffset(1000, -2000) == 0);
    REQUIRE(Common::ApplyRtcOffset(1000, 250) == 1250);
    REQUIRE(Common::ApplyRtcOffset(1000, 5'000'000'000LL) == 5'000'001'000LL);
    REQUIRE(Common::ApplyRtcOffset(1000, (std::numeric_limits<s64>::max)()) ==
            (std::numeric_limits<s64>::max)());
    REQUIRE(Common::ApplyRtcOffset(1000, (std::numeric_limits<s64>::min)()) == 0);
    REQUIRE(Common::ApplyRtcOffset((std::numeric_limits<s64>::min)(), -1) == 0);
}
