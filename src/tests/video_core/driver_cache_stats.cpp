// SPDX-License-Identifier: GPL-3.0-or-later
#include <fstream>
#include <catch2/catch_test_macros.hpp>
#include "common/scope_exit.h"
#include "video_core/driver_cache_stats.h"

TEST_CASE("Driver cache age uses actual write times and reports missing or bounded scans",
          "[driver_cache_stats]") {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        ("tgm-cache-stats-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "nested");
    SCOPE_EXIT { fs::remove_all(root); };
    REQUIRE_FALSE(VideoCommon::DriverCacheAgeSeconds(VideoCommon::ReadDriverCacheStats(root)));
    REQUIRE(VideoCommon::ReadDriverCacheStats(root / "missing").incomplete);
    std::ofstream(root / "older").write("123", 3);
    std::ofstream(root / "nested" / "newer").write("12345", 5);
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(root / "older", now - std::chrono::hours{2});
    fs::last_write_time(root / "nested" / "newer", now - std::chrono::minutes{5});
    const auto stats = VideoCommon::ReadDriverCacheStats(root);
    REQUIRE(stats.files == 2);
    REQUIRE(stats.bytes == 8);
    REQUIRE_FALSE(stats.incomplete);
    REQUIRE(VideoCommon::DriverCacheAgeSeconds(stats, now) == 300);
    fs::last_write_time(root / "nested" / "newer", now + std::chrono::minutes{5});
    REQUIRE(VideoCommon::DriverCacheAgeSeconds(VideoCommon::ReadDriverCacheStats(root), now) == 0);
    REQUIRE(VideoCommon::ReadDriverCacheStats(root, 1).incomplete);
}
