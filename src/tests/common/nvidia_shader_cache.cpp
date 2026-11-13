// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/nvidia_shader_cache.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>
#include <catch2/catch_test_macros.hpp>

namespace {
class CacheEnvironment {
public:
    CacheEnvironment() {
        for (size_t i = 0; i < names.size(); ++i) {
            if (const char* value = std::getenv(names[i])) {
                previous[i] = value;
            }
            unsetenv(names[i]);
        }
        std::filesystem::create_directories(root);
        setenv("XDG_CACHE_HOME", root.c_str(), 1);
    }
    ~CacheEnvironment() {
        for (size_t i = 0; i < names.size(); ++i) {
            if (previous[i]) {
                setenv(names[i], previous[i]->c_str(), 1);
            } else {
                unsetenv(names[i]);
            }
        }
        std::error_code error;
        std::filesystem::permissions(root, std::filesystem::perms::owner_all, error);
        std::filesystem::remove_all(root, error);
    }
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("tgm-nvidia-cache-test-" + std::to_string(getpid()) + "-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
private:
    const std::array<const char*, 4> names{"HOME", "XDG_CACHE_HOME", "__GL_SHADER_DISK_CACHE_PATH",
                                         "__GL_SHADER_DISK_CACHE_SIZE"};
    std::array<std::optional<std::string>, 4> previous;
};
}

TEST_CASE("Linux NVIDIA cache defaults preserve overrides and usable locations", "[nvidia_disk_cache]") {
    CacheEnvironment environment;
    SECTION("XDG location and a private Eden cache") {
        Common::SetUpNvidiaShaderDiskCache();
        REQUIRE(std::getenv("__GL_SHADER_DISK_CACHE_PATH") != nullptr);
        CHECK(std::filesystem::path{std::getenv("__GL_SHADER_DISK_CACHE_PATH")} ==
              environment.root / "eden/nvidia");
        CHECK(std::filesystem::is_directory(environment.root / "eden/nvidia"));
        CHECK(std::string{std::getenv("__GL_SHADER_DISK_CACHE_SIZE")} == "10737418240");
    }
    SECTION("Even empty user values remain user choices") {
        setenv("__GL_SHADER_DISK_CACHE_PATH", "", 1);
        setenv("__GL_SHADER_DISK_CACHE_SIZE", "0", 1);
        Common::SetUpNvidiaShaderDiskCache();
        CHECK(std::string{std::getenv("__GL_SHADER_DISK_CACHE_PATH")}.empty());
        CHECK(std::string{std::getenv("__GL_SHADER_DISK_CACHE_SIZE")} == "0");
        CHECK_FALSE(std::filesystem::exists(environment.root / "eden"));
    }
    SECTION("Relative XDG falls back to an absolute home") {
        setenv("XDG_CACHE_HOME", "relative", 1);
        setenv("HOME", environment.root.c_str(), 1);
        Common::SetUpNvidiaShaderDiskCache();
        CHECK(std::filesystem::path{std::getenv("__GL_SHADER_DISK_CACHE_PATH")} ==
              environment.root / ".cache/eden/nvidia");
    }
    SECTION("A failed folder never publishes an unusable path") {
        const auto file = environment.root / "file";
        std::ofstream{file} << "occupied";
        setenv("XDG_CACHE_HOME", file.c_str(), 1);
        Common::SetUpNvidiaShaderDiskCache();
        CHECK(std::getenv("__GL_SHADER_DISK_CACHE_PATH") == nullptr);
    }
}

TEST_CASE("NVIDIA cache creation denial is handled without privileged access", "[nvidia_disk_cache][permissions]") {
    if (geteuid() == 0) {
        SKIP("Run this case as an unprivileged user");
    }
    CacheEnvironment environment;
    std::filesystem::permissions(environment.root,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
    Common::SetUpNvidiaShaderDiskCache();
    CHECK(std::getenv("__GL_SHADER_DISK_CACHE_PATH") == nullptr);
    CHECK_FALSE(std::filesystem::exists(environment.root / "eden"));
}
#endif
