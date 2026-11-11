// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "common/fs/path_util.h"
#include "frontend_common/data_manager.h"
#include "frontend_common/mod_manager.h"

namespace {
namespace fs = std::filesystem;
using Common::FS::EdenPath;
using FrontendCommon::DataManager::DataDir;

class DataDirectoryFixture {
public:
    explicit DataDirectoryFixture(const std::u8string& name) {
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            original[i] = Common::FS::GetEdenPath(kinds[i]);
        }
        root = fs::temp_directory_path() /
               ("fritchii-data-directories-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        REQUIRE(fs::create_directory(root));
        const auto prefix = root / name;
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            const auto path = prefix / std::to_string(i);
            REQUIRE(fs::create_directories(path));
            Common::FS::SetEdenPath(kinds[i], path);
            REQUIRE(Common::FS::GetEdenPath(kinds[i]) == path);
        }
    }

    ~DataDirectoryFixture() {
        // SetEdenPath cannot restore default directories that do not exist yet.
        Common::FS::SetAppDirectory({});
        std::error_code ec;
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            if (fs::is_directory(original[i], ec)) {
                Common::FS::SetEdenPath(kinds[i], original[i]);
            }
        }
        fs::remove_all(root, ec);
    }

    fs::path Expected(DataDir dir, const std::string& user_id) const {
        switch (dir) {
        case DataDir::Saves:
            return Common::FS::GetEdenPath(EdenPath::SaveDir) / "user" / "save" /
                   "0000000000000000" / user_id;
        case DataDir::UserNand:
            return Common::FS::GetEdenPath(EdenPath::NANDDir) / "user" / "Contents" / "registered";
        case DataDir::SysNand:
            return Common::FS::GetEdenPath(EdenPath::NANDDir) / "system" / "Contents" /
                   "registered";
        case DataDir::Mods:
            return Common::FS::GetEdenPath(EdenPath::LoadDir);
        case DataDir::Shaders:
            return Common::FS::GetEdenPath(EdenPath::ShaderDir);
        }
        return {};
    }

    fs::path root;

private:
    static constexpr std::array kinds{EdenPath::NANDDir, EdenPath::SaveDir, EdenPath::LoadDir,
                                      EdenPath::ShaderDir};
    std::array<fs::path, kinds.size()> original;
};
} // namespace

TEST_CASE("Data directory paths preserve native characters", "[frontend_common][data_manager]") {
    const auto name = GENERATE(std::u8string{u8"ASCII"}, std::u8string{u8"\u6e38\u620f-\u00e9"});
    const auto dir = GENERATE(DataDir::Saves, DataDir::UserNand, DataDir::SysNand, DataDir::Mods,
                              DataDir::Shaders);
    DataDirectoryFixture fixture(name);
    const std::string user_id = "0123456789abcdef0123456789abcdef";
    REQUIRE(FrontendCommon::DataManager::GetDataDir(dir, user_id) ==
            fixture.Expected(dir, user_id));
}

TEST_CASE("Data directory operations use the configured native path",
          "[frontend_common][data_manager]") {
    const auto name = GENERATE(std::u8string{u8"ASCII"}, std::u8string{u8"\u6e38\u620f-\u00e9"});
    const auto dir = GENERATE(DataDir::Saves, DataDir::UserNand, DataDir::SysNand, DataDir::Mods,
                              DataDir::Shaders);
    DataDirectoryFixture fixture(name);
    const std::string user_id = "0123456789abcdef0123456789abcdef";
    const auto expected = fixture.Expected(dir, user_id);
    REQUIRE(FrontendCommon::DataManager::GetDataDirString(dir, user_id) ==
            Common::FS::PathToUTF8String(expected));
    REQUIRE(fs::is_directory(expected));
    {
        std::ofstream file(expected / "payload", std::ios::binary);
        REQUIRE(file.write("abc", 3).good());
    }
    REQUIRE(FrontendCommon::DataManager::DataDirSize(dir) == 3U);
    const auto sentinel = fixture.root / "keep";
    REQUIRE(std::ofstream(sentinel).put('x').good());
    REQUIRE(FrontendCommon::DataManager::ClearDir(dir, user_id) == 2U);
    REQUIRE(fs::is_directory(expected));
    REQUIRE(fs::is_empty(expected));
    REQUIRE(fs::exists(sentinel));
}

TEST_CASE("Mod installation preserves native paths", "[frontend_common][mod_manager]") {
    const auto name = GENERATE(std::u8string{u8"ASCII"}, std::u8string{u8"\u6e38\u620f-\u00e9"});
    const bool copy = GENERATE(true, false);
    DataDirectoryFixture fixture(name);
    const auto source = fixture.root / "source" / name;
    REQUIRE(fs::create_directories(source / "romfs"));
    REQUIRE(std::ofstream(source / "romfs/payload").write("abc", 3).good());
    const auto load = Common::FS::GetEdenPath(EdenPath::LoadDir);
    const auto installed = load / "0100000000000000" / source.filename();
    REQUIRE(fs::create_directories(installed));
    REQUIRE(std::ofstream(installed / "stale").put('x').good());
    REQUIRE(std::ofstream(load / "keep").put('x').good());

    REQUIRE(FrontendCommon::InstallMod(source, 0x0100000000000000ULL, copy) ==
            FrontendCommon::Success);
    REQUIRE(fs::file_size(installed / "romfs/payload") == 3U);
    REQUIRE_FALSE(fs::exists(installed / "stale"));
    REQUIRE(fs::exists(load / "keep"));
    REQUIRE(fs::exists(source) == copy);
}

TEST_CASE("Mod discovery preserves UTF-8 paths", "[frontend_common][mod_manager]") {
    const auto name = GENERATE(std::u8string{u8"ASCII"}, std::u8string{u8"\u6e38\u620f-\u00e9"});
    DataDirectoryFixture fixture(name);
    const auto collection = fixture.root / name;
    const auto first = collection / name;
    const auto second = collection / "another-mod";
    REQUIRE(fs::create_directories(first / "romfs"));
    REQUIRE(fs::create_directories(second / "exefs"));
    auto actual = FrontendCommon::GetModFolder(Common::FS::PathToUTF8String(collection));
    std::vector<fs::path> expected{first, second};
    std::sort(actual.begin(), actual.end());
    std::sort(expected.begin(), expected.end());
    REQUIRE(actual == expected);
}

TEST_CASE("UTF-8 selected mod paths install without ANSI conversion",
          "[frontend_common][mod_manager]") {
    const auto name = GENERATE(std::u8string{u8"ASCII"}, std::u8string{u8"\u6e38\u620f-\u00e9"});
    const bool copy = GENERATE(true, false);
    DataDirectoryFixture fixture(name);
    const auto source = fixture.root / "selected" / name;
    REQUIRE(fs::create_directories(source / "exefs"));
    REQUIRE(std::ofstream(source / "exefs/payload").write("abc", 3).good());
    const auto installed =
        Common::FS::GetEdenPath(EdenPath::LoadDir) / "0100000000000000" / source.filename();
    REQUIRE(fs::create_directories(installed.parent_path()));
    const auto selected_utf8 = Common::FS::PathToUTF8String(source);
    REQUIRE(FrontendCommon::InstallModFromUTF8(selected_utf8, 0x0100000000000000ULL, copy) ==
            FrontendCommon::Success);
    REQUIRE(fs::file_size(installed / "exefs/payload") == 3U);
    REQUIRE(fs::exists(source) == copy);
}
