// SPDX-FileCopyrightText: Copyright 2026 Lichii Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <memory>
#include <set>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/fs/path_util.h"
#include "common/scope_exit.h"
#include "tests/core/system_test_context.h"

TEST_CASE("Nested system test contexts isolate both backing directories",
          "[core][test_system_directory_isolation]") {
    using Common::FS::EdenPath;
    const auto previous_nand = Common::FS::GetEdenPath(EdenPath::NANDDir);
    const auto previous_sdmc = Common::FS::GetEdenPath(EdenPath::SDMCDir);
    std::vector<std::unique_ptr<ScopedSystemTestContext>> contexts;
    SCOPE_EXIT {
        while (!contexts.empty()) {
            contexts.pop_back();
        }
    };
    std::set<std::filesystem::path> directories;
    for (int i = 0; i < 64; ++i) {
        contexts.push_back(std::make_unique<ScopedSystemTestContext>());
        const auto nand = Common::FS::GetEdenPath(EdenPath::NANDDir);
        const auto sdmc = Common::FS::GetEdenPath(EdenPath::SDMCDir);
        CHECK(sdmc == nand / "sdmc");
        CHECK(std::filesystem::is_directory(nand / "sdmc"));
        CHECK(directories.insert(nand).second);
    }
    while (!contexts.empty()) {
        contexts.pop_back();
    }
    CHECK(Common::FS::GetEdenPath(EdenPath::NANDDir) == previous_nand);
    CHECK(Common::FS::GetEdenPath(EdenPath::SDMCDir) == previous_sdmc);
    for (const auto& directory : directories) {
        CHECK_FALSE(std::filesystem::exists(directory));
    }
}
