// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "common/fs/path_util.h"
#include "frontend_common/mod_manager.h"

namespace {
namespace fs = std::filesystem;

class ModInstallFixture {
public:
    ModInstallFixture() {
        root = fs::temp_directory_path() /
               ("fritchii-mod-install-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        REQUIRE(fs::create_directory(root));
        REQUIRE(fs::create_directory(root / "load"));
        REQUIRE(fs::create_directory(Installed().parent_path()));
        Common::FS::SetEdenPath(Common::FS::EdenPath::LoadDir, root / "load");
        REQUIRE(Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir) == root / "load");
    }

    ~ModInstallFixture() {
        Common::FS::SetAppDirectory({});
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    fs::path Installed() const {
        return root / "load" / "0100000000000000" / "SampleMod";
    }

    void WriteMod(const fs::path& path, const std::string& text) const {
        REQUIRE(fs::create_directories(path / "romfs"));
        REQUIRE(std::ofstream(path / "romfs" / "payload").write(text.data(), text.size()).good());
    }

    std::string ReadPayload(const fs::path& path) const {
        std::ifstream file(path / "romfs" / "payload");
        std::string text;
        std::getline(file, text);
        return text;
    }

    fs::path root;
};

constexpr u64 ProgramId = 0x0100000000000000ULL;

class CurrentDirectoryGuard {
public:
    explicit CurrentDirectoryGuard(const fs::path& path) : original{fs::current_path()} {
        fs::current_path(path);
    }

    ~CurrentDirectoryGuard() {
        std::error_code ec;
        fs::current_path(original, ec);
    }

private:
    fs::path original;
};
} // namespace

TEST_CASE("Reinstalling a mod from its installed folder preserves it", "[frontend_common][mod_manager]") {
    const bool copy = GENERATE(true, false);
    const bool dotted_path = GENERATE(false, true);
    ModInstallFixture fixture;
    const auto installed = fixture.Installed();
    fixture.WriteMod(installed, "keep this mod");
    REQUIRE(std::ofstream(installed.parent_path() / "keep").put('x').good());
    const auto selected = dotted_path ? installed.parent_path() / "." / installed.filename()
                                      : installed;
    const auto mods = FrontendCommon::GetModFolder(selected.string());
    REQUIRE(mods.size() == 1);
    REQUIRE(fs::equivalent(mods.front(), installed));

    CHECK(FrontendCommon::InstallMod(mods.front(), ProgramId, copy) == FrontendCommon::Success);
    CHECK(fixture.ReadPayload(installed) == "keep this mod");
    CHECK(fs::exists(installed.parent_path() / "keep"));
}

TEST_CASE("Installing a separate mod still copies or moves and replaces old files",
          "[frontend_common][mod_manager]") {
    const bool copy = GENERATE(true, false);
    const bool replace = GENERATE(false, true);
    ModInstallFixture fixture;
    const auto source = fixture.root / "download" / "SampleMod";
    const auto installed = fixture.Installed();
    fixture.WriteMod(source, "new mod");
    if (replace) {
        fixture.WriteMod(installed, "old mod");
        REQUIRE(std::ofstream(installed / "stale").put('x').good());
    }

    REQUIRE(FrontendCommon::InstallMod(source, ProgramId, copy) == FrontendCommon::Success);
    CHECK(fixture.ReadPayload(installed) == "new mod");
    CHECK_FALSE(fs::exists(installed / "stale"));
    CHECK(fs::exists(source) == copy);
}

TEST_CASE("A missing mod source cannot remove an installed destination",
          "[frontend_common][mod_manager]") {
    const bool copy = GENERATE(true, false);
    ModInstallFixture fixture;
    const auto installed = fixture.Installed();
    fixture.WriteMod(installed, "keep this mod");
    const auto missing = fixture.root / "missing" / installed.filename();

    CHECK(FrontendCommon::InstallMod(missing, ProgramId, copy) == FrontendCommon::Failed);
    CHECK(fixture.ReadPayload(installed) == "keep this mod");
}

TEST_CASE("A source inside its install destination cannot be deleted during installation",
          "[frontend_common][mod_manager]") {
    const bool copy = GENERATE(true, false);
    ModInstallFixture fixture;
    const auto installed = fixture.Installed();
    fixture.WriteMod(installed, "keep installed");
    const auto source = installed / "nested" / installed.filename();
    fixture.WriteMod(source, "keep selected");
    REQUIRE(std::ofstream(installed.parent_path() / "keep").put('x').good());

    CHECK(FrontendCommon::InstallMod(source, ProgramId, copy) == FrontendCommon::Failed);
    CHECK(fixture.ReadPayload(installed) == "keep installed");
    CHECK(fixture.ReadPayload(source) == "keep selected");
    CHECK(fs::exists(installed.parent_path() / "keep"));
}

TEST_CASE("A source containing its install destination cannot copy or move into itself",
          "[frontend_common][mod_manager]") {
    const bool copy = GENERATE(true, false);
    const bool aliased = GENERATE(false, true);
    ModInstallFixture fixture;
    const auto source = fixture.root / "download" / "SampleMod";
    fixture.WriteMod(source, "keep selected");
    const auto load = source / "install-root";
    REQUIRE(fs::create_directory(load));
    Common::FS::SetEdenPath(Common::FS::EdenPath::LoadDir, load);
    const auto installed = load / "0100000000000000" / "SampleMod";
    fixture.WriteMod(installed, "keep installed");
    fs::path selected = source;
    if (aliased) {
        std::error_code error;
        fs::create_directory_symlink(source.parent_path(), fixture.root / "alias", error);
        if (error) {
            SKIP("Directory symlink creation is unavailable: " << error.message());
        }
        selected = fixture.root / "alias" / source.filename();
        REQUIRE(fs::equivalent(selected, source));
    }

    CHECK(FrontendCommon::InstallMod(selected, ProgramId, copy) == FrontendCommon::Failed);
    CHECK(fixture.ReadPayload(source) == "keep selected");
    CHECK(fixture.ReadPayload(installed) == "keep installed");
}

TEST_CASE("Mod installation accepts relative paths and trailing separators",
          "[frontend_common][mod_manager]") {
    const bool copy = GENERATE(true, false);
    const bool replace = GENERATE(false, true);
    const auto spelling = GENERATE(0, 1, 2, 3);
    ModInstallFixture fixture;
    const auto source = fixture.root / "download" / "SampleMod";
    const auto installed = fixture.Installed();
    fixture.WriteMod(source, "new mod");
    if (replace) {
        fixture.WriteMod(installed, "old mod");
        REQUIRE(std::ofstream(installed / "stale").put('x').good());
    }
    CurrentDirectoryGuard current_directory(fixture.root);
    const auto selected = spelling == 0 ? fs::path{"download/SampleMod"}
                          : spelling == 1 ? fs::path{"download/SampleMod/"}
                          : spelling == 2 ? source / ""
                                          : source / ".";

    REQUIRE(FrontendCommon::InstallMod(selected, ProgramId, copy) == FrontendCommon::Success);
    CHECK(fixture.ReadPayload(installed) == "new mod");
    CHECK_FALSE(fs::exists(installed / "stale"));
    CHECK(fs::exists(source) == copy);
}

TEST_CASE("A mod selected as the current directory uses its folder name",
          "[frontend_common][mod_manager]") {
    ModInstallFixture fixture;
    const auto source = fixture.root / "download" / "SampleMod";
    fixture.WriteMod(source, "new mod");
    CurrentDirectoryGuard current_directory(source);

    REQUIRE(FrontendCommon::InstallMod(".", ProgramId) == FrontendCommon::Success);
    CHECK(fixture.ReadPayload(fixture.Installed()) == "new mod");
    CHECK(fixture.ReadPayload(source) == "new mod");
}

TEST_CASE("Equivalent source spellings do not remove installed mods or their siblings",
          "[frontend_common][mod_manager]") {
    const bool copy = GENERATE(true, false);
    const auto spelling = GENERATE(0, 1, 2, 3);
    ModInstallFixture fixture;
    const auto installed = fixture.Installed();
    fixture.WriteMod(installed, "keep this mod");
    REQUIRE(std::ofstream(installed.parent_path() / "keep").put('x').good());
    CurrentDirectoryGuard current_directory(installed);
    const auto selected = spelling == 0 ? fs::path{"."}
                          : spelling == 1 ? fs::path{"../SampleMod"}
                          : spelling == 2 ? installed / ""
                                          : installed / ".";

    CHECK(FrontendCommon::InstallMod(selected, ProgramId, copy) == FrontendCommon::Success);
    CHECK(fixture.ReadPayload(installed) == "keep this mod");
    CHECK(fs::exists(installed.parent_path() / "keep"));
}

TEST_CASE("Installing through an aliased parent recognizes the installed folder",
          "[frontend_common][mod_manager]") {
    const bool copy = GENERATE(true, false);
    ModInstallFixture fixture;
    const auto installed = fixture.Installed();
    fixture.WriteMod(installed, "keep this mod");
    fs::path selected;
    if (const auto junction = std::getenv("FRITCHII_TEST_TEMP_ALIAS")) {
        selected = fs::path{junction} / fixture.root.filename() / "load" /
                   installed.parent_path().filename() / installed.filename();
    } else {
        std::error_code ec;
        fs::create_directory_symlink(installed.parent_path(), fixture.root / "alias", ec);
        if (ec) {
            SKIP("Directory symlink creation is unavailable: " << ec.message());
        }
        selected = fixture.root / "alias" / installed.filename();
    }
    REQUIRE(fs::equivalent(selected, installed));

    CHECK(FrontendCommon::InstallMod(selected, ProgramId, copy) == FrontendCommon::Success);
    CHECK(fixture.ReadPayload(installed) == "keep this mod");
}

#ifdef _WIN32
TEST_CASE("Reinstalling a mod with different path casing preserves it",
          "[frontend_common][mod_manager]") {
    ModInstallFixture fixture;
    const auto installed = fixture.Installed();
    fixture.WriteMod(installed, "keep this mod");
    const auto selected = installed.parent_path() / "samplemod";
    REQUIRE(fs::equivalent(selected, installed));

    CHECK(FrontendCommon::InstallMod(selected, ProgramId) == FrontendCommon::Success);
    CHECK(fixture.ReadPayload(installed) == "keep this mod");
}
#endif
