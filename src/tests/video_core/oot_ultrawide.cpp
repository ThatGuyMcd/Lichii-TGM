// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "core/oot_ultrawide.h"
#include "video_core/oot_ultrawide.h"

using namespace VideoCommon::OotUltrawide;

TEST_CASE("Ocarina's 21:9 frames and 16:9 pictures", "[oot_ultrawide]") {
    REQUIRE(IsWideFrame(3440, 1440));
    REQUIRE(IsWideFrame(2560, 1080));
    REQUIRE_FALSE(IsWideFrame(2560, 1440));
    REQUIRE(IsSixteenNine(1920, 1080));
    REQUIRE(IsSixteenNine(960, 540));
    REQUIRE_FALSE(IsSixteenNine(3440, 1440));
}

TEST_CASE("The movie stand-in fills a 21:9 output from a 16:9 movie", "[oot_ultrawide]") {
    REQUIRE(IsWideMovieOutput(1920, 1080, 3440, 1440));
    REQUIRE(IsWideMovieOutput(960, 540, 3440, 1440));
    // 16:9 outputs take the usual same-aspect path; wide inputs are not movies.
    REQUIRE_FALSE(IsWideMovieOutput(1920, 1080, 2560, 1440));
    REQUIRE_FALSE(IsWideMovieOutput(3440, 1440, 3440, 1440));
    // An output smaller than the input, or more than 4x it, is not the movie network's.
    REQUIRE_FALSE(IsWideMovieOutput(1920, 1080, 1720, 720));
    REQUIRE_FALSE(IsWideMovieOutput(320, 180, 3440, 1440));
}

TEST_CASE("Movie replacement matches each selected aspect profile", "[oot_ultrawide]") {
    for (const auto& profile : Core::OotUltrawide::AspectProfiles) {
        INFO(profile.addon);
        REQUIRE(IsAspectMovieOutput(1920, 1080, profile.width, 1440, profile.width, 1440));
        REQUIRE_FALSE(IsAspectMovieOutput(1920, 1080, 2560, 1440, profile.width, 1440));
        REQUIRE_FALSE(IsAspectMovieOutput(3440, 1440, profile.width, 1440, profile.width, 1440));
        REQUIRE_FALSE(IsAspectMovieOutput(320, 180, profile.width, 1440, profile.width, 1440));
        for (const auto& other : Core::OotUltrawide::AspectProfiles) {
            if (other.aspect != profile.aspect) {
                REQUIRE_FALSE(IsAspectMovieOutput(1920, 1080, other.width, 1440,
                                                 profile.width, 1440));
            }
        }
    }
    REQUIRE_FALSE(IsAspectMovieOutput(1920, 1080, 1920, 1440, 0, 1440));
}
