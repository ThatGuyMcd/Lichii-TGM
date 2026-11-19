// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "common/fs/fs_paths.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/scope_exit.h"

TEST_CASE("The test log backend stays isolated when a test changes data paths",
          "[.][test_log_isolation]") {
    namespace fs = std::filesystem;
    using namespace Common::FS;
    const fs::path isolated = GetEdenPath(EdenPath::LogDir);
    const fs::path decoy = isolated.parent_path() / "decoy-owner-log";
    REQUIRE(fs::create_directory(decoy));
    const fs::path sentinel = decoy / LOG_FILE;
    const std::string original = "Independent synthetic prior gameplay log\n";
    { std::ofstream file{sentinel}; file << original; }
    SetEdenPath(EdenPath::LogDir, decoy);
    SCOPE_EXIT { SetEdenPath(EdenPath::LogDir, isolated); };
    Common::Log::Initialize();
    LOG_CRITICAL(Debug, "Independent test log isolation marker");
    Common::Log::Stop();
    const auto read = [](const fs::path& path) {
        std::ifstream file{path};
        return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    };
    CHECK(read(sentinel) == original);
    CHECK_FALSE(fs::exists(sentinel.string() + ".old.txt"));
    CHECK(read(isolated / LOG_FILE).find("Independent test log isolation marker") != std::string::npos);
}
