// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/fs/path_util.h"
#include "common/scope_exit.h"
#include "core/file_sys/savedata_factory.h"
#include "core/hle/service/acc/profile_manager.h"
#include "tests/core/system_test_context.h"
#ifndef _WIN32
#include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;
using Service::Account::ProfileManager;
using Common::FS::EdenPath;

fs::path SaveRoot() {
    const auto path = Common::FS::GetEdenPath(EdenPath::NANDDir) / "user/save/0000000000000000";
    fs::create_directories(path);
    return path;
}
void Put(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream file{path, std::ios::binary};
    REQUIRE(file.is_open());
    file << value;
    REQUIRE(file.good());
}
std::string Read(const fs::path& path) {
    std::ifstream file{path, std::ios::binary};
    REQUIRE(file.is_open());
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}
}

TEST_CASE("Orphan cleanup leaves folders that are not complete user IDs untouched",
          "[profile_orphan_cleanup]") {
    ScopedSystemTestContext context;
    ProfileManager manager;
    const auto base = SaveRoot();
    const std::string user_id = "ABCDEF0123456789ABCDEF0123456789";
    const std::vector<std::string> ignored{
        "backups", "0100123456789000", user_id.substr(1), user_id+"0",
        "G"+user_id.substr(1), user_id+".backup", "prefix-"+user_id};
    for (const auto& name : ignored) {
        REQUIRE(fs::create_directory(base/name));
    }
    Put(base/"copied-title"/"sentinel.dat", "Independent non-profile content");
    const auto orphaned = manager.FindOrphanedProfiles();
    CHECK(orphaned.empty());
    for (const auto& name : ignored) {
        CHECK(fs::is_directory(base/name));
    }
    CHECK(Read(base/"copied-title"/"sentinel.dat") == "Independent non-profile content");
}

TEST_CASE("Orphan cleanup preserves saved users and reports real nonempty orphan IDs",
          "[profile_orphan_cleanup]") {
    ScopedSystemTestContext context;
    ProfileManager manager;
    manager.WriteUserSaveFile();
    const auto users = manager.GetAllUsers();
    const auto profile_file = Common::FS::GetEdenPath(EdenPath::NANDDir) /
        "system/save/8000000000000010/su/avators/profiles.dat";
    const auto profiles = Read(profile_file);
    const auto base = SaveRoot();
    auto registered = manager.FindExistingProfileStrings().at(0);
    std::transform(registered.begin(), registered.end(), registered.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string orphan = "abcdef0123456789abcdef0123456789";
    const std::string metadata = "0123456789abcdef0123456789abcdef";
    const std::string empty = "fedcba9876543210fedcba9876543210";
    Put(base/registered/"save.dat", "Independent registered save");
    Put(base/orphan/"save.dat", "Independent orphan save");
    Put(base/metadata/FileSys::GetSaveDataSizeFileName(), "Independent size marker");
    Put(base/"00000000000000000000000000000000"/"save.dat", "Independent system save");
    REQUIRE(fs::create_directories(base/empty/"empty-child"));
    const auto orphaned = manager.FindOrphanedProfiles();
    CHECK(orphaned == std::vector<std::string>{orphan});
    CHECK_FALSE(fs::exists(base/empty));
    CHECK(Read(base/registered/"save.dat") == "Independent registered save");
    CHECK(Read(base/orphan/"save.dat") == "Independent orphan save");
    CHECK(Read(base/metadata/FileSys::GetSaveDataSizeFileName()) == "Independent size marker");
    CHECK(Read(base/"00000000000000000000000000000000"/"save.dat") == "Independent system save");
    CHECK(manager.GetAllUsers() == users);
    CHECK(Read(profile_file) == profiles);
}

#ifndef _WIN32
TEST_CASE("An unreadable profile subtree is retained without failing the scan",
          "[profile_orphan_cleanup]") {
    ScopedSystemTestContext context;
    ProfileManager manager;
    const auto profile = SaveRoot()/manager.FindExistingProfileStrings().at(0);
    Put(profile/"save.dat", "Independent inaccessible save");
    const auto permissions = fs::status(profile).permissions();
    fs::permissions(profile, fs::perms::none);
    SCOPE_EXIT { std::error_code ec; fs::permissions(profile, permissions, ec); };
    if (geteuid() != 0) {
        std::error_code error;
        fs::directory_iterator denied{profile, error};
        REQUIRE(error == std::errc::permission_denied);
    }
    std::vector<std::string> orphaned;
    CHECK_NOTHROW(orphaned = manager.FindOrphanedProfiles());
    CHECK(orphaned.empty());
    CHECK(fs::exists(profile));
    fs::permissions(profile, permissions);
    CHECK(Read(profile/"save.dat") == "Independent inaccessible save");
}
#endif
