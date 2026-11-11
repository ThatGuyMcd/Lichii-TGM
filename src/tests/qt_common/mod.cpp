// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "common/fs/fs_util.h"
#include "qt_common/abstract/frontend.h"
#include "qt_common/util/mod.h"

namespace {
namespace fs = std::filesystem;
QString answer;
int errors{};

class ModFolderFixture {
public:
    ModFolderFixture() {
        root = fs::temp_directory_path() /
               ("fritchii-mod-rename-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        REQUIRE(fs::create_directory(root));
        REQUIRE(fs::create_directories(Source() / "romfs"));
        REQUIRE(std::ofstream(Source() / "romfs" / "payload").write("keep mod", 8).good());
        answer = QStringLiteral("Alpha");
        errors = 0;
    }

    ~ModFolderFixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    fs::path Source() const {
        return root / "Alpha";
    }

    QString Select(const fs::path& path) const {
        return QString::fromStdU16String(path.u16string());
    }

    std::string Read(const fs::path& path) const {
        std::ifstream file(path);
        std::string text;
        std::getline(file, text);
        return text;
    }

    fs::path root;
};

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

// Only frontend responses are replaced: discovery and rename use production code.
namespace QtCommon {
QWidget* rootObject = nullptr;

const QString tr(const char* text) {
    return QString::fromUtf8(text);
}

namespace Frontend {
const QString GetTextInput(const QString&, const QString&, const QString&) {
    return answer;
}

int Choice(const QString&, const QString&, const QStringList&) {
    FAIL_CHECK("A recognized mod must not need a mod-type choice");
    return -1;
}

StandardButton ShowMessage(Icon icon, const QString&, const QString&, StandardButtons, QObject*) {
    CHECK(icon == Icon::Critical);
    ++errors;
    return StandardButton::Ok;
}
} // namespace Frontend
} // namespace QtCommon

TEST_CASE("Mod naming preserves a colliding sibling", "[qt_common][mod_rename]") {
    const auto kind = GENERATE(0, 1, 2);
    ModFolderFixture fixture;
    const auto destination = fixture.root / "Beta";
    if (kind == 2) {
        REQUIRE(std::ofstream(destination).write("keep sibling", 12).good());
    } else {
        REQUIRE(fs::create_directory(destination));
        if (kind == 1) {
            REQUIRE(std::ofstream(destination / "sentinel").write("keep sibling", 12).good());
        }
    }
    answer = QStringLiteral("Beta");

    const auto result = QtCommon::Mod::GetModFolders(fixture.Select(fixture.Source()), {});
    CHECK(result.isEmpty());
    CHECK(errors == 1);
    CHECK(fixture.Read(fixture.Source() / "romfs" / "payload") == "keep mod");
    REQUIRE(fs::exists(destination));
    if (kind == 0) {
        CHECK(fs::is_empty(destination));
    } else {
        CHECK(fixture.Read(kind == 1 ? destination / "sentinel" : destination) == "keep sibling");
    }
}

TEST_CASE("An unused mod name renames the selected folder", "[qt_common][mod_rename]") {
    ModFolderFixture fixture;
    answer = QStringLiteral("Beta");
    const auto destination = fixture.root / "Beta";

    const auto result = QtCommon::Mod::GetModFolders(fixture.Select(fixture.Source()), {});
    REQUIRE(result.size() == 1);
    CHECK(fs::equivalent(fs::path{result.front().toStdU16String()}, destination));
    CHECK_FALSE(fs::exists(fixture.Source()));
    CHECK(fixture.Read(destination / "romfs" / "payload") == "keep mod");
    CHECK(errors == 0);
}

TEST_CASE("A dangling symlink is an occupied mod name", "[qt_common][mod_rename]") {
    ModFolderFixture fixture;
    const auto destination = fixture.root / "Beta";
    const auto missing = fixture.root / "missing-target";
    std::error_code error;
    fs::create_directory_symlink(missing, destination, error);
    if (error) {
        SKIP("Directory symlink creation is unavailable: " << error.message());
    }
    REQUIRE(fs::is_symlink(fs::symlink_status(destination)));
    REQUIRE_FALSE(fs::exists(destination));
    answer = QStringLiteral("Beta");

    CHECK(QtCommon::Mod::GetModFolders(fixture.Select(fixture.Source()), {}).isEmpty());
    CHECK(errors == 1);
    CHECK(fixture.Read(fixture.Source() / "romfs" / "payload") == "keep mod");
    CHECK(fs::is_symlink(fs::symlink_status(destination)));
    CHECK(fs::read_symlink(destination) == missing);
}

TEST_CASE("Mod naming preserves Unicode through discovery, rename and the returned selection",
          "[qt_common][mod_rename]") {
    const bool rename = GENERATE(false, true);
    ModFolderFixture fixture;
    const auto source = fixture.root / fs::path{u8"\u6e38\u620f-\u00e9"};
    fs::rename(fixture.Source(), source);
    const auto destination = rename ? fixture.root / fs::path{u8"\u6797\u95f4-\u00f6"} : source;
    answer = QString::fromStdU16String(destination.filename().u16string());

    const auto result = QtCommon::Mod::GetModFolders(fixture.Select(source), {});
    REQUIRE(result.size() == 1);
    CHECK(fs::equivalent(fs::path{Common::FS::ToU8String(result.front().toStdString())},
                         destination));
    CHECK(fixture.Read(destination / "romfs" / "payload") == "keep mod");
    CHECK(errors == 0);
}

TEST_CASE("Keeping a mod name leaves its files intact", "[qt_common][mod_rename]") {
    ModFolderFixture fixture;
    const auto result = QtCommon::Mod::GetModFolders(fixture.Select(fixture.Source()), {});
    REQUIRE(result.size() == 1);
    CHECK(fs::equivalent(fs::path{result.front().toStdU16String()}, fixture.Source()));
    CHECK(fixture.Read(fixture.Source() / "romfs" / "payload") == "keep mod");
    CHECK(errors == 0);
}

TEST_CASE("Cancelling mod naming leaves source and siblings intact", "[qt_common][mod_rename]") {
    ModFolderFixture fixture;
    REQUIRE(std::ofstream(fixture.root / "sentinel").put('x').good());
    answer = {};

    CHECK(QtCommon::Mod::GetModFolders(fixture.Select(fixture.Source()), {}).isEmpty());
    CHECK(fixture.Read(fixture.Source() / "romfs" / "payload") == "keep mod");
    CHECK(fixture.Read(fixture.root / "sentinel") == "x");
    CHECK(errors == 0);
}

TEST_CASE("Relative mod selections support unchanged and new names", "[qt_common][mod_rename]") {
    const bool rename = GENERATE(false, true);
    ModFolderFixture fixture;
    CurrentDirectoryGuard current_directory(fixture.root);
    answer = rename ? QStringLiteral("Beta") : QStringLiteral("Alpha");
    const auto destination = fixture.root / (rename ? "Beta" : "Alpha");

    const auto result = QtCommon::Mod::GetModFolders(QStringLiteral("./Alpha"), {});
    REQUIRE(result.size() == 1);
    CHECK(fs::equivalent(fs::path{result.front().toStdU16String()}, destination));
    CHECK(fixture.Read(destination / "romfs" / "payload") == "keep mod");
    CHECK(errors == 0);
}

#ifdef _WIN32
TEST_CASE("Equivalent Windows mod names cannot delete the source", "[qt_common][mod_rename]") {
    ModFolderFixture fixture;
    answer = QStringLiteral("alpha");
    REQUIRE(fs::equivalent(fixture.Source(), fixture.root / "alpha"));

    const auto result = QtCommon::Mod::GetModFolders(fixture.Select(fixture.Source()), {});
    REQUIRE(result.size() == 1);
    CHECK(fs::equivalent(fs::path{result.front().toStdU16String()}, fixture.Source()));
    CHECK(fixture.Read(fixture.Source() / "romfs" / "payload") == "keep mod");
    CHECK(errors == 0);
}

TEST_CASE("Equivalent Windows mod names through a parent alias preserve the source",
          "[qt_common][mod_rename]") {
    ModFolderFixture fixture;
    fs::path selected;
    if (const auto alias = std::getenv("FRITCHII_TEST_TEMP_ALIAS")) {
        selected = fs::path{alias} / fixture.root.filename() / "Alpha";
    } else {
        std::error_code ec;
        fs::create_directory_symlink(fixture.root, fixture.root / "alias", ec);
        if (ec) {
            SKIP("Directory symlink creation is unavailable: " << ec.message());
        }
        selected = fixture.root / "alias" / "Alpha";
    }
    REQUIRE(fs::equivalent(selected, fixture.Source()));
    answer = QStringLiteral("alpha");

    const auto result = QtCommon::Mod::GetModFolders(fixture.Select(selected), {});
    REQUIRE(result.size() == 1);
    CHECK(fs::equivalent(fs::path{result.front().toStdU16String()}, fixture.Source()));
    CHECK(fixture.Read(fixture.Source() / "romfs" / "payload") == "keep mod");
    CHECK(errors == 0);
}
#endif
