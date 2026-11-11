// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
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
