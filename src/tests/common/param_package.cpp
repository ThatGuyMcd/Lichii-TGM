// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <math.h>
#include "common/logging.h"
#include "common/param_package.h"

namespace Common {

TEST_CASE("ParamPackage", "[common]") {
    ParamPackage original{
        {"abc", "xyz"},
        {"def", "42"},
        {"jkl", "$$:1:$2$,3"},
    };
    original.Set("ghi", 3.14f);
    ParamPackage copy(original.Serialize());
    REQUIRE(copy.Get("abc", "") == "xyz");
    REQUIRE(copy.Get("def", 0) == 42);
    REQUIRE(std::abs(copy.Get("ghi", 0.0f) - 3.14f) < 0.01f);
    REQUIRE(copy.Get("jkl", "") == "$$:1:$2$,3");
    REQUIRE(copy.Get("mno", "uvw") == "uvw");
    REQUIRE(copy.Get("abc", 42) == 42);
}

TEST_CASE("ParamPackage preserves explicit empty values and escaped separators", "[common]") {
    ParamPackage original{{"empty", ""}, {"escaped", ":,$"}};
    ParamPackage copy{original.Serialize()};
    REQUIRE(copy.Has("empty"));
    REQUIRE(copy.Get("empty", "fallback").empty());
    REQUIRE(copy.Get("escaped", "") == ":,$");
    ParamPackage malformed{"bad:a:b,missing,valid:"};
    REQUIRE_FALSE(malformed.Has("bad"));
    REQUIRE_FALSE(malformed.Has("missing"));
    REQUIRE(malformed.Has("valid"));
    REQUIRE(malformed.Get("valid", "fallback").empty());
}

} // namespace Common
