// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include "video_core/texture_cache/eviction_age.h"

TEST_CASE("Diagnostic age preserves routine collection but cannot block emergency eviction",
          "[video_core][texture_cache]") {
    using VideoCommon::TextureGcEffectiveMinAge;
    for (const u64 diagnostic : {0ULL, 600ULL, 9000ULL}) {
        CHECK(TextureGcEffectiveMinAge(50, diagnostic, false) == (std::max)(u64{50}, diagnostic));
        CHECK(TextureGcEffectiveMinAge(25, diagnostic, false) == (std::max)(u64{25}, diagnostic));
        CHECK(TextureGcEffectiveMinAge(10, diagnostic, true) == 10);
        CHECK(TextureGcEffectiveMinAge(3, diagnostic, true) == 3);
    }
}

TEST_CASE("Texture emergency collection responds to pressure and stays bounded",
          "[video_core][texture_cache]") {
    using VideoCommon::TextureGcWorkFor;
    constexpr u64 GiB = u64{1} << 30;
    const auto routine = TextureGcWorkFor(8 * GiB, 4 * GiB, 600, true, false);
    CHECK(routine.min_age == 600);
    CHECK(routine.iterations == 20);
    const auto at_limit = TextureGcWorkFor(4 * GiB, 4 * GiB, 600, true, true);
    CHECK(at_limit.iterations == 40);
    CHECK(at_limit.min_age == 10);
    const auto pressured = TextureGcWorkFor(5 * GiB, 4 * GiB, 600, true, true);
    CHECK(pressured.iterations > 55); // Can keep up with the guard-house allocation burst.
    CHECK(pressured.min_age == 3); // Diagnostic age cannot block emergency eviction.
    const auto bounded = TextureGcWorkFor((std::numeric_limits<u64>::max)(), 0, 0, true, true);
    CHECK(bounded.iterations == 1064);
    const auto below = TextureGcWorkFor(0, 4 * GiB, 0, true, true);
    CHECK(below.iterations == 40); // No subtraction underflow during explicit OOM collection.
}
