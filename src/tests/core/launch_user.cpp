// SPDX-FileCopyrightText: Copyright 2026 Lichii-TGM contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <string>
#include <vector>
#include <getopt.h>
#include <catch2/catch_test_macros.hpp>
#include "common/settings.h"
#include "common/fs/path_util.h"
#include "core/core.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/launch_params.h"
#include "tests/core/system_test_context.h"

namespace {
Core::LaunchParams ParseUser(Core::System& system, std::vector<std::string> options,
                            bool defer_profile_selection = false) {
    options.insert(options.begin(), "profile-test");
    std::vector<char*> argv;
    for (auto& option : options)
        argv.push_back(option.data());
    argv.push_back(nullptr);
#ifdef __APPLE__
    optreset = 1;
    optind = 1;
#else
    optind = 0;
#endif
    return Core::ParseLaunchParams(system, static_cast<int>(options.size()), argv.data(), nullptr,
                                  defer_profile_selection);
}

void AddUsers(Core::System& system) {
    auto& profiles = system.GetProfileManager();
    REQUIRE(profiles.CreateNewUser(Common::UUID::MakeRandom(), "Second").IsSuccess());
    REQUIRE(profiles.CreateNewUser(Common::UUID::MakeRandom(), "1st Adventurer").IsSuccess());
    profiles.WriteUserSaveFile();
}
}

TEST_CASE("Launch user names beginning with a digit remain names", "[core][launch_user]") {
    ScopedSystemTestContext context;
    Settings::values.current_user = 0;
    Core::System system;
    AddUsers(system);
    const auto parsed = ParseUser(system, {"-u", "1st Adventurer"});
    REQUIRE(parsed.selected_user.has_value());
    CHECK(*parsed.selected_user == 2);
    CHECK(Settings::values.current_user.GetValue() == 2);
}

TEST_CASE("A later unknown launch user does not retain a numeric selection",
          "[core][launch_user]") {
    ScopedSystemTestContext context;
    Settings::values.current_user = 0;
    Core::System system;
    AddUsers(system);
    const auto parsed = ParseUser(system, {"-u", "1", "-u", "Missing"});
    CHECK_FALSE(parsed.selected_user.has_value());
}

TEST_CASE("An out-of-range launch index cannot silently select the last profile",
          "[core][launch_user]") {
    ScopedSystemTestContext context;
    Settings::values.current_user = 0;
    Core::System system;
    AddUsers(system);
    const auto parsed = ParseUser(system, {"-u", "8"});
    CHECK_FALSE(parsed.selected_user.has_value());
    Core::ApplyLaunchParams(parsed);
    CHECK(Settings::values.current_user.GetValue() == 0);
}

TEST_CASE("CLI launch names resolve in the configured NAND instead of the previous NAND",
          "[core][launch_user]") {
    ScopedSystemTestContext context;
    Settings::values.current_user = 0;
    Core::System system;
    AddUsers(system);
    std::string requested{"Second"};
    SECTION("The same name has another index in the configured NAND") {}
    SECTION("The name exists only in the configured NAND") { requested = "Target Only"; }
    const auto parsed = ParseUser(system, {"-u", requested}, true);
    REQUIRE_FALSE(parsed.selected_user.has_value());
    CHECK(parsed.selected_user_name == requested);

    const auto configured = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "configured";
    REQUIRE(std::filesystem::create_directory(configured));
    Common::FS::SetEdenPath(Common::FS::EdenPath::NANDDir, configured);
    Service::Account::ProfileManager target;
    REQUIRE(target.CreateNewUser(Common::UUID::MakeRandom(), "Other").IsSuccess());
    REQUIRE(target.CreateNewUser(Common::UUID::MakeRandom(), requested).IsSuccess());
    target.WriteUserSaveFile();
    const auto expected = target.GetUser(2);
    Settings::values.current_user = 0; // Value read from the selected configuration.

    Core::ApplyLaunchParams(parsed);
    Core::ReloadLaunchProfile(parsed, system);
    CHECK(Settings::values.current_user.GetValue() == 2);
    REQUIRE(system.GetProfileManager().GetOpenUserCount() == 1);
    CHECK(system.GetProfileManager().GetOpenUsers()[0] == *expected);
    CHECK(system.GetProfileManager().GetAllUsers() == target.GetAllUsers());
}

TEST_CASE("CLI numeric selection is applied after the configured users are loaded",
          "[core][launch_user]") {
    ScopedSystemTestContext context;
    Settings::values.current_user = 0;
    Core::System system;
    const auto parsed = ParseUser(system, {"-u", "2"}, true);
    REQUIRE(parsed.selected_user == 2);
    CHECK(parsed.selected_user_name.empty());
    CHECK(Settings::values.current_user.GetValue() == 0);

    const auto configured = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "configured";
    REQUIRE(std::filesystem::create_directory(configured));
    Common::FS::SetEdenPath(Common::FS::EdenPath::NANDDir, configured);
    Service::Account::ProfileManager target;
    REQUIRE(target.CreateNewUser(Common::UUID::MakeRandom(), "Other").IsSuccess());
    REQUIRE(target.CreateNewUser(Common::UUID::MakeRandom(), "Target").IsSuccess());
    target.WriteUserSaveFile();
    Core::ApplyLaunchParams(parsed);
    Core::ReloadLaunchProfile(parsed, system);
    REQUIRE(system.GetProfileManager().GetOpenUserCount() == 1);
    CHECK(system.GetProfileManager().GetOpenUsers()[0] == *target.GetUser(2));
}

TEST_CASE("CLI repeated user flags preserve the last choice and reject invalid indices",
          "[core][launch_user]") {
    ScopedSystemTestContext context;
    Settings::values.current_user = 0;
    Core::System system;
    AddUsers(system);
    SECTION("A numeric index replaces a name") {
        const auto parsed = ParseUser(system, {"-u", "Second", "-u", "2"}, true);
        CHECK(parsed.selected_user == 2);
        CHECK(parsed.selected_user_name.empty());
    }
    SECTION("A name replaces a numeric index") {
        const auto parsed = ParseUser(system, {"-u", "2", "-u", "Second"}, true);
        CHECK_FALSE(parsed.selected_user.has_value());
        CHECK(parsed.selected_user_name == "Second");
    }
    for (const auto& argument : {std::string{}, std::string(100, '9'), std::string{"8"}}) {
        const auto parsed = ParseUser(system, {"-u", argument}, true);
        CHECK_FALSE(parsed.selected_user.has_value());
        Settings::values.current_user = 1;
        Core::ApplyLaunchParams(parsed);
        CHECK(Settings::values.current_user.GetValue() == 1);
    }
}
