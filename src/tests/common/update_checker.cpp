// SPDX-FileCopyrightText: Copyright 2026 Lichii Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "common/httplib.h"
#include "common/net/net.h"
#include "common/scm_rev.h"
#include "frontend_common/update_checker.h"

namespace {

std::string DownloadUrl(std::string_view tag, std::string_view filename) {
    return "https://github.com/" + std::string{Common::g_build_auto_update_repo} +
           "/releases/download/" + std::string{tag} + "/" + std::string{filename};
}

Common::Net::Release MakeRelease(std::string tag, std::string filename) {
    const nlohmann::json json{
        {"tag_name", tag},
        {"name", "Lichii-tgm " + tag},
        {"prerelease", true},
        {"assets", {{{"browser_download_url", DownloadUrl(tag, filename)}}}},
    };
    return Common::Net::Release::FromJson(json, "https://api.github.com",
                                          Common::g_build_auto_update_repo)
        .value();
}

Common::Net::Release MakeForkRelease(std::string tag, std::string_view platform = "windows-x86_64",
                                     std::string_view extension = ".zip") {
    auto release = MakeRelease(tag, tag + "-" + std::string{platform} + std::string{extension});
    release.prerelease = false;
    return release;
}

} // namespace

TEST_CASE("Lichii-tgm versions order major, minor and patch numbers independently", "[updater]") {
    const auto releases = std::array{
        MakeForkRelease("Lichii-tgm-v1.9"),  MakeForkRelease("Lichii-tgm-v1.10.1"),
        MakeForkRelease("Lichii-tgm-v1.10"), MakeForkRelease("Lichii-tgm-v1.0.1"),
        MakeForkRelease("Lichii-tgm-v1.0"),
    };
    for (const auto current :
         {"Lichii-tgm-v1.0", "Lichii-tgm-v1.0.0", "Lichii-tgm-v1.9", "Lichii-tgm-v1.10"}) {
        const auto update = UpdateChecker::SelectUpdate(releases, current, "windows-x86_64");
        REQUIRE(update);
        CHECK(update->tag == "Lichii-tgm-v1.10.1");
    }
    CHECK_FALSE(UpdateChecker::SelectUpdate(releases, "Lichii-tgm-v1.10.1", "windows-x86_64"));
    CHECK_FALSE(UpdateChecker::SelectUpdate(releases, "Lichii-tgm-v2.0", "windows-x86_64"));
    const auto next_major = MakeForkRelease("Lichii-tgm-v2.0");
    CHECK(UpdateChecker::SelectUpdate(std::array{next_major}, "Lichii-tgm-v1.999.999",
                                      "windows-x86_64"));
    CHECK_FALSE(UpdateChecker::SelectUpdate(std::array{MakeForkRelease("Lichii-tgm-v1.0.0")},
                                            "Lichii-tgm-v1.0", "windows-x86_64"));
    CHECK_FALSE(UpdateChecker::SelectUpdate(std::array{MakeForkRelease("Lichii-tgm-v1.0")},
                                            "Lichii-tgm-v1.0.0", "windows-x86_64"));
    const auto patch = UpdateChecker::SelectUpdate(std::array{MakeForkRelease("Lichii-tgm-v1.0.1")},
                                                   "Lichii-tgm-v1.0", "windows-x86_64");
    REQUIRE(patch);
    CHECK(patch->tag == "Lichii-tgm-v1.0.1");
}

TEST_CASE("Fork and legacy release channels cannot replace one another", "[updater]") {
    const auto releases = std::array{
        MakeRelease("build-999999", "lichii-windows-x86_64.zip"),
        MakeForkRelease("Lichii-tgm-v1.1"),
    };
    const auto fork = UpdateChecker::SelectUpdate(releases, "Lichii-tgm-v1.0", "windows-x86_64");
    REQUIRE(fork);
    CHECK(fork->tag == "Lichii-tgm-v1.1");
    const auto legacy = UpdateChecker::SelectUpdate(releases, "build-70", "windows-x86_64");
    REQUIRE(legacy);
    CHECK(legacy->tag == "build-999999");
    CHECK_FALSE(
        UpdateChecker::SelectUpdate(std::array{releases[0]}, "Lichii-tgm-v1.0", "windows-x86_64"));
    CHECK_FALSE(UpdateChecker::SelectUpdate(std::array{releases[1]}, "build-70", "windows-x86_64"));
}

TEST_CASE("Stable fork updates ignore prereleases and unsupported version syntax", "[updater]") {
    const std::string malformed =
        GENERATE("Lichii-tgm-v", "Lichii-tgm-v1", "Lichii-tgm-v1.", "Lichii-tgm-v1..0",
                 "Lichii-tgm-v1.0.", "Lichii-tgm-v1.0.0.1", "Lichii-tgm-v01.1", "Lichii-tgm-v1.01",
                 "Lichii-tgm-v1.0.01", "Lichii-tgm-v1.+1", "Lichii-tgm-v1.-1", "Lichii-tgm-v1. 1",
                 "Lichii-tgm-v1.0rc1", "Lichii-tgm-v2.0-rc.1", "Lichii-tgm-v2.0+dev",
                 "Lichii-tgm-v18446744073709551616.0", "Lichii-tgm-v1.18446744073709551616",
                 "Lichii-tgm-v1.0.18446744073709551616", "lichii-tgm-v1.1", "other-v1.1");
    const auto valid = MakeForkRelease("Lichii-tgm-v1.1");
    CHECK_FALSE(UpdateChecker::SelectUpdate(std::array{valid}, malformed, "windows-x86_64"));
    CHECK_FALSE(UpdateChecker::SelectUpdate(std::array{MakeForkRelease(malformed)},
                                            "Lichii-tgm-v1.0", "windows-x86_64"));
    auto preview = MakeForkRelease("Lichii-tgm-v99.0");
    preview.prerelease = true;
    const auto update = UpdateChecker::SelectUpdate(std::array{preview, valid}, "Lichii-tgm-v1.0",
                                                    "windows-x86_64");
    REQUIRE(update);
    CHECK(update->tag == valid.tag);
    CHECK_FALSE(
        UpdateChecker::SelectUpdate(std::array{preview}, "Lichii-tgm-v1.0", "windows-x86_64"));
}

TEST_CASE("Fork updates require the release tag, platform and package in their asset name",
          "[updater]") {
    const std::string platform = GENERATE("windows-x86_64", "linux-x86_64");
    const std::string extension = platform.starts_with("linux-") ? ".AppImage" : ".zip";
    const auto release = MakeForkRelease("Lichii-tgm-v1.1", platform, extension);
    const auto selected = release.GetPlatformAssets(platform);
    REQUIRE(selected.size() == 1);
    CHECK(selected[0].filename == "Lichii-tgm-v1.1-" + platform + extension);
    CHECK(selected[0].url + selected[0].path == DownloadUrl(release.tag, selected[0].filename));
    const auto update =
        UpdateChecker::SelectUpdate(std::array{release}, "Lichii-tgm-v1.0", platform);
    REQUIRE(update);
    CHECK(update->tag == "Lichii-tgm-v1.1");
    for (const auto& filename :
         {"lichii-" + platform + extension, "Lichii-tgm-v1.0-" + platform + extension,
          "not-Lichii-tgm-v1.1-" + platform + extension,
          "Lichii-tgm-v1.1-" + platform + "-pgo" + extension,
          "Lichii-tgm-v1.1-" + platform + extension + ".sha256"}) {
        auto wrong = release;
        wrong.assets = {DownloadUrl(wrong.tag, filename)};
        CHECK(wrong.GetPlatformAssets(platform).empty());
        CHECK_FALSE(UpdateChecker::SelectUpdate(std::array{wrong}, "Lichii-tgm-v1.0", platform));
    }
    CHECK(
        release.GetPlatformAssets(platform == "windows-x86_64" ? "linux-x86_64" : "windows-x86_64")
            .empty());
    if (platform.starts_with("linux-")) {
        CHECK(release.GetPlatformAssets(platform, Common::Net::LinuxPackage::Archive).empty());
    }
}

TEST_CASE("Fork PGO preference and asset integrity metadata survive selection", "[updater]") {
    auto release = MakeForkRelease("Lichii-tgm-v1.1");
    const auto standard = release.GetPlatformAssets("windows-x86_64-pgo");
    REQUIRE(standard.size() == 1);
    CHECK(standard[0].filename == "Lichii-tgm-v1.1-windows-x86_64.zip");
    const std::string pgo_name = "Lichii-tgm-v1.1-windows-x86_64-pgo.zip";
    const auto url = DownloadUrl(release.tag, pgo_name);
    release.assets.push_back(url);
    release.asset_info[url] = {.sha256 = std::string(64, 'a'), .size = 1234};
    const auto pgo = release.GetPlatformAssets("windows-x86_64-pgo");
    REQUIRE(pgo.size() == 1);
    CHECK(pgo[0].filename == pgo_name);
    CHECK(pgo[0].sha256 == std::string(64, 'a'));
    CHECK(pgo[0].size == 1234);
    CHECK(release.GetPlatformAssets("windows-x86_64")[0].filename == standard[0].filename);
}

TEST_CASE("Lichii updates compare build numbers, not tag strings", "[updater]") {
    const auto releases = std::array{
        MakeRelease("build-9", "lichii-windows-x86_64.zip"),
        MakeRelease("build-100", "lichii-windows-x86_64.zip"),
        MakeRelease("build-99", "lichii-windows-x86_64.zip"),
    };
    const auto update = UpdateChecker::SelectUpdate(releases, "build-70", "windows-x86_64");
    REQUIRE(update);
    CHECK(update->tag == "build-100");
    CHECK_FALSE(UpdateChecker::SelectUpdate(releases, "build-100", "windows-x86_64"));
    CHECK_FALSE(UpdateChecker::SelectUpdate(releases, "build-101", "windows-x86_64"));
    CHECK_FALSE(UpdateChecker::SelectUpdate({}, "build-70", "windows-x86_64"));
}

TEST_CASE("Lichii ignores dev builds and malformed release tags", "[updater]") {
    const std::string tag =
        GENERATE("", "nx2", "v0.2.0", "abc123-nx2", "nx2-build-71", "build-", "build-0", "build--1",
                 "build-+71", "build-71junk", "build- 71", "build-18446744073709551616");
    const auto releases = std::array{MakeRelease("build-71", "lichii-windows-x86_64.zip")};
    CHECK_FALSE(UpdateChecker::SelectUpdate(releases, tag, "windows-x86_64"));
    auto invalid = releases;
    invalid[0].tag = tag;
    invalid[0].assets = {DownloadUrl(tag, "lichii-windows-x86_64.zip")};
    CHECK_FALSE(UpdateChecker::SelectUpdate(invalid, "build-70", "windows-x86_64"));
}

TEST_CASE("Lichii selects only the matching platform and PGO variant", "[updater]") {
    const std::string platform =
        GENERATE("windows-x86_64", "windows-x86_64-pgo", "linux-x86_64", "linux-x86_64-pgo",
                 "linux-aarch64", "linux-aarch64-pgo", "macos-arm64", "macos-x86_64");
    auto release = MakeRelease("build-71", "unrelated.zip");
    for (const auto name :
         {"windows-x86_64.zip", "windows-x86_64-pgo.zip", "linux-x86_64.tar.gz",
          "linux-x86_64-pgo.tar.gz", "linux-aarch64.tar.gz", "linux-aarch64-pgo.tar.gz",
          "macos-arm64.zip", "macos-x86_64.zip", "linux-x86_64.AppImage",
          "linux-x86_64-pgo.AppImage", "linux-aarch64.AppImage", "linux-aarch64-pgo.AppImage"}) {
        release.assets.push_back(DownloadUrl("build-71", "lichii-" + std::string{name}));
    }
    const auto selected = release.GetPlatformAssets(platform);
    REQUIRE(selected.size() == 1);
    CHECK(selected[0].filename ==
          "lichii-" + platform + (platform.starts_with("linux-") ? ".AppImage" : ".zip"));
    CHECK(selected[0].url == "https://github.com");
    CHECK(selected[0].path == "/" + std::string{Common::g_build_auto_update_repo} +
                                  "/releases/download/build-71/" + selected[0].filename);
    CHECK_FALSE(release.GetPlatformAssets("android-arm64").size());
    CHECK_FALSE(release.GetPlatformAssets("").size());
}

TEST_CASE("Lichii skips releases without its asset and falls back to Linux archives", "[updater]") {
    const auto releases = std::array{
        MakeRelease("build-73", "lichii-linux-x86_64.AppImage"),
        MakeRelease("build-71", "lichii-linux-x86_64-pgo.tar.gz"),
        MakeRelease("build-72", "lichii-linux-aarch64-pgo.AppImage"),
    };
    const auto update = UpdateChecker::SelectUpdate(releases, "build-70", "linux-x86_64");
    REQUIRE(update);
    CHECK(update->tag == "build-73");
    CHECK(update->GetPlatformAssets("linux-x86_64")[0].filename == "lichii-linux-x86_64.AppImage");
    CHECK_FALSE(UpdateChecker::SelectUpdate(releases, "build-70", "windows-x86_64"));

    const auto archive = UpdateChecker::SelectUpdate(releases, "build-70", "linux-x86_64-pgo",
                                                     Common::Net::LinuxPackage::Archive);
    REQUIRE(archive);
    CHECK(archive->tag == "build-71");
    CHECK(archive->GetPlatformAssets("linux-x86_64-pgo", Common::Net::LinuxPackage::Archive)[0]
              .filename == "lichii-linux-x86_64-pgo.tar.gz");
}

TEST_CASE("PGO builds fall back to the standard asset, standard builds never get PGO",
          "[updater]") {
    auto release = MakeRelease("build-71", "lichii-windows-x86_64.zip");
    const auto pgo = release.GetPlatformAssets("windows-x86_64-pgo");
    REQUIRE(pgo.size() == 1);
    CHECK(pgo[0].name == "Standard");
    CHECK(pgo[0].filename == "lichii-windows-x86_64.zip");

    release.assets.push_back(DownloadUrl("build-71", "lichii-windows-x86_64-pgo.zip"));
    const auto preferred = release.GetPlatformAssets("windows-x86_64-pgo");
    REQUIRE(preferred.size() == 1);
    CHECK(preferred[0].name == "PGO");
    CHECK(preferred[0].filename == "lichii-windows-x86_64-pgo.zip");
    CHECK(release.GetPlatformAssets("windows-x86_64")[0].filename == "lichii-windows-x86_64.zip");

    const auto pgo_only = MakeRelease("build-72", "lichii-windows-x86_64-pgo.zip");
    CHECK(pgo_only.GetPlatformAssets("windows-x86_64").empty());
    CHECK_FALSE(UpdateChecker::SelectUpdate(std::array{pgo_only}, "build-70", "windows-x86_64"));
    CHECK(UpdateChecker::SelectUpdate(std::array{pgo_only}, "build-70", "windows-x86_64-pgo"));
    CHECK(release.GetPlatformAssets("macos-arm64-pgo").empty());
    CHECK(release.GetPlatformAssets("macos-x86_64-pgo").empty());
}

TEST_CASE("Linux installs only get the package they were installed from", "[updater]") {
    auto release = MakeRelease("build-71", "lichii-linux-x86_64.AppImage");
    release.assets.push_back(DownloadUrl("build-71", "lichii-linux-x86_64.tar.gz"));
    CHECK(release.GetPlatformAssets("linux-x86_64", Common::Net::LinuxPackage::AppImage)[0]
              .filename == "lichii-linux-x86_64.AppImage");
    CHECK(
        release.GetPlatformAssets("linux-x86_64", Common::Net::LinuxPackage::Archive)[0].filename ==
        "lichii-linux-x86_64.tar.gz");

    const auto appimage_only = MakeRelease("build-72", "lichii-linux-x86_64.AppImage");
    CHECK(appimage_only.GetPlatformAssets("linux-x86_64", Common::Net::LinuxPackage::Archive)
              .empty());
}

TEST_CASE("Asset digest and size come from the release JSON", "[updater]") {
    const std::string sha(64, 'A');
    const nlohmann::json json{
        {"tag_name", "build-71"},
        {"assets",
         {{{"browser_download_url", DownloadUrl("build-71", "lichii-windows-x86_64.zip")},
           {"size", u64{1234}},
           {"digest", "sha256:" + sha}},
          {{"browser_download_url", DownloadUrl("build-71", "lichii-linux-x86_64.AppImage")},
           {"size", u64{5}},
           {"digest", "sha256:zz"}},
          {{"browser_download_url", DownloadUrl("build-71", "lichii-linux-x86_64.tar.gz")},
           {"digest", "md5:abc"}}}},
    };
    const auto release = Common::Net::Release::FromJson(json, "https://api.github.com",
                                                        Common::g_build_auto_update_repo)
                             .value();
    const auto windows = release.GetPlatformAssets("windows-x86_64");
    REQUIRE(windows.size() == 1);
    CHECK(windows[0].sha256 == std::string(64, 'a'));
    CHECK(windows[0].size == 1234);
    const auto appimage = release.GetPlatformAssets("linux-x86_64");
    REQUIRE(appimage.size() == 1);
    CHECK(appimage[0].sha256.empty());
    CHECK(appimage[0].size == 5);
    const auto archive =
        release.GetPlatformAssets("linux-x86_64", Common::Net::LinuxPackage::Archive);
    REQUIRE(archive.size() == 1);
    CHECK(archive[0].sha256.empty());
    CHECK(archive[0].size == 0);
}

TEST_CASE("Failed release requests are classified by status", "[updater]") {
    CHECK(UpdateChecker::ClassifyFailure(403) == UpdateChecker::Failure::RateLimited);
    CHECK(UpdateChecker::ClassifyFailure(429) == UpdateChecker::Failure::RateLimited);
    CHECK(UpdateChecker::ClassifyFailure(404) == UpdateChecker::Failure::Http);
    CHECK(UpdateChecker::ClassifyFailure(500) == UpdateChecker::Failure::Http);
    CHECK(UpdateChecker::ClassifyFailure(0) == UpdateChecker::Failure::Network);
    CHECK(UpdateChecker::ClassifyFailure(200) == UpdateChecker::Failure::Network);
}

TEST_CASE("Lichii-tgm does not select upstream or lookalike download URLs", "[updater]") {
    const auto release_url = DownloadUrl("build-71", "lichii-windows-x86_64.zip");
    const auto wrong_repo = std::string_view{Common::g_build_auto_update_repo} == "zortos293/Lichii"
                                ? "eden-emu/eden"
                                : "zortos293/Lichii";
    const std::array urls{
        "https://github.com/" + std::string{wrong_repo} +
            "/releases/download/build-71/lichii-windows-x86_64.zip",
        DownloadUrl("build-70", "lichii-windows-x86_64.zip"),
        DownloadUrl("build-71", "not-lichii-windows-x86_64.zip"),
        "https://github.com.example.com/" + release_url.substr(19),
    };
    for (const auto& url : urls) {
        auto release = MakeRelease("build-71", "lichii-windows-x86_64.zip");
        release.assets = {url};
        CHECK(release.GetPlatformAssets("windows-x86_64").empty());
    }
}

TEST_CASE("GitHub failures and invalid JSON produce no releases", "[updater]") {
    const std::string_view json =
        GENERATE("", "not JSON", R"({"message":"API rate limit exceeded"})", "null", "[]",
                 R"([{"tag_name":42}])", R"([{"tag_name":"build-71","assets":null}])",
                 R"([{"tag_name":"build-71","draft":true}])");
    CHECK(Common::Net::Release::ListFromJson(json, "https://api.github.com",
                                             Common::g_build_auto_update_repo)
              .empty());
}

TEST_CASE("GitHub optional fields can be null or missing", "[updater]") {
    const std::string_view json = R"([{"tag_name":"build-71","name":null,"body":null}])";
    const auto releases = Common::Net::Release::ListFromJson(json, "https://api.github.com",
                                                             Common::g_build_auto_update_repo);
    REQUIRE(releases.size() == 1);
    CHECK(releases[0].title == "build-71");
    CHECK(releases[0].assets.empty());
    CHECK(releases[0].html_url == "https://github.com/" +
                                      std::string{Common::g_build_auto_update_repo} +
                                      "/releases/tag/build-71");
}

TEST_CASE("Release requests handle HTTP failures without throwing", "[updater]") {
    const int status = GENERATE(200, 403, 429, 500);
    httplib::Server server;
    server.Get("/releases", [status](const httplib::Request& request, httplib::Response& response) {
        response.status =
            request.get_header_value("User-Agent") == "Lichii-tgm" &&
                    request.get_header_value("Accept") == "application/vnd.github+json"
                ? status
                : 400;
        response.set_content("[]", "application/json");
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    const std::jthread listener([&server](std::stop_token stop) {
        const std::stop_callback stop_server(stop, [&server] { server.stop(); });
        server.listen_after_bind();
    });
    server.wait_until_ready();
    const auto response =
        Common::Net::MakeRequest("http://127.0.0.1:" + std::to_string(port), "/releases");
    CHECK(response.has_value() == (status == 200));
}
