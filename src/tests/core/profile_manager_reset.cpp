// SPDX-FileCopyrightText: Copyright 2026 Lichii-TGM contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "core/hle/service/acc/profile_manager.h"
#include "tests/core/system_test_context.h"

namespace {
using Service::Account::ProfileManager;
using Common::FS::EdenPath;

std::string SavedProfiles() {
    const auto path = Common::FS::GetEdenPath(EdenPath::NANDDir) /
                      "system/save/8000000000000010/su/avators/profiles.dat";
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.is_open());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
}

TEST_CASE("Profile reset reopens the selected user without rewriting saved profiles",
          "[core][acc][profile_reset]") {
    ScopedSystemTestContext context;
    Settings::values.current_user = 0;
    ProfileManager manager;
    REQUIRE(manager.CreateNewUser(Common::UUID::MakeRandom(), "Second").IsSuccess());
    manager.WriteUserSaveFile();
    const auto users = manager.GetAllUsers();
    const auto original = SavedProfiles();
    Settings::values.current_user = 1;

    manager.ResetUserSaveFile();
    REQUIRE(manager.GetUserCount() == 2);
    REQUIRE(manager.GetOpenUserCount() == 1);
    CHECK(manager.GetOpenUsers()[0] == users[1]);
    CHECK(manager.GetAllUsers() == users);
    CHECK(Settings::values.current_user.GetValue() == 1);
    CHECK(SavedProfiles() == original);

    manager.ResetUserSaveFile();
    CHECK(manager.GetOpenUserCount() == 1);
    CHECK(manager.GetOpenUsers()[0] == users[1]);
    CHECK(SavedProfiles() == original);
}

TEST_CASE("Profile reset creates and opens a default on an empty configured NAND",
          "[core][acc][profile_reset]") {
    ScopedSystemTestContext context;
    ProfileManager manager;
    const auto old_user = manager.GetUser(0);
    const auto empty = Common::FS::GetEdenPath(EdenPath::NANDDir) / "configured-nand";
    REQUIRE(std::filesystem::create_directory(empty));
    Common::FS::SetEdenPath(EdenPath::NANDDir, empty);
    Settings::values.current_user = 7;

    manager.ResetUserSaveFile();
    REQUIRE(manager.GetUserCount() == 1);
    REQUIRE(manager.GetOpenUserCount() == 1);
    REQUIRE(manager.GetUser(0).has_value());
    CHECK(manager.GetUser(0) != old_user);
    CHECK(manager.GetOpenUsers()[0] == *manager.GetUser(0));
    CHECK(Settings::values.current_user.GetValue() == 0);
    CHECK_FALSE(SavedProfiles().empty());
}

TEST_CASE("Profile reset falls back from an absent current user",
          "[core][acc][profile_reset]") {
    ScopedSystemTestContext context;
    Settings::values.current_user = 0;
    ProfileManager manager;
    const auto expected = manager.GetUser(0);
    Settings::values.current_user = 7;
    manager.ResetUserSaveFile();
    REQUIRE(manager.GetOpenUserCount() == 1);
    CHECK(manager.GetOpenUsers()[0] == *expected);
    CHECK(Settings::values.current_user.GetValue() == 0);
}
