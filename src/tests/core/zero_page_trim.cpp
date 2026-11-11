// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdexcept>
#include <catch2/catch_test_macros.hpp>
#include "core/zero_page_trim.h"

TEST_CASE("Optional zero trimming contains allocation failure and keeps successful results",
          "[core][memory]") {
    size_t calls{};
    const auto failed = Core::TryTrimZeroPages([&]() -> std::pair<size_t, size_t> {
        ++calls;
        throw std::bad_alloc{};
    });
    CHECK_FALSE(failed.has_value());
    CHECK(calls == 1);
    const auto success = Core::TryTrimZeroPages([] { return std::pair<size_t, size_t>{128, 32}; });
    REQUIRE(success.has_value());
    CHECK(success->first == 128);
    CHECK(success->second == 32);
    CHECK_THROWS_AS(Core::TryTrimZeroPages([]() -> std::pair<size_t, size_t> {
        throw std::logic_error("unrelated failure");
    }), std::logic_error);
}
