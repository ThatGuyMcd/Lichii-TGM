// SPDX-License-Identifier: GPL-3.0-or-later
#include <chrono>
#include <filesystem>
#include <fstream>
#include <catch2/catch_test_macros.hpp>
#include "common/scope_exit.h"
#include "video_core/vulkan_common/ngx_runtime_path.h"

TEST_CASE("NGX runtime discovery retains unversioned priority and compares numeric versions",
          "[ngx_paths]") {
    const auto root = std::filesystem::temp_directory_path() /
        ("tgm-ngx-paths-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    SCOPE_EXIT { std::filesystem::remove_all(root); };
    const auto touch = [&](const char* name) { std::ofstream(root / name).put('x'); };
    REQUIRE(Vulkan::Ngx::FindRuntimeInDirectory(root).empty());
    REQUIRE(Vulkan::Ngx::FindRuntimeInDirectory(root / "absent").empty());
    touch("libnvidia-ngx-dlss.so.9.9");
    touch("libnvidia-ngx-dlss.so.10.1");
    touch("libnvidia-ngx-dlss.so.10.0");
    touch("libnvidia-ngx-dlss.so.999.backup");
    std::filesystem::create_directory(root / "libnvidia-ngx-dlss.so.999");
    REQUIRE(Vulkan::Ngx::FindRuntimeInDirectory(root).filename() == "libnvidia-ngx-dlss.so.10.1");
    touch("libnvngx_dlss.so");
    REQUIRE(Vulkan::Ngx::FindRuntimeInDirectory(root).filename() == "libnvngx_dlss.so");
    touch("libnvidia-ngx-dlss.so");
    REQUIRE(Vulkan::Ngx::FindRuntimeInDirectory(root).filename() == "libnvidia-ngx-dlss.so");
    std::filesystem::remove(root / "libnvidia-ngx-dlss.so");
    std::filesystem::remove(root / "libnvngx_dlss.so");
    REQUIRE(Vulkan::Ngx::FindRuntimeInDirectory(root).filename() == "libnvidia-ngx-dlss.so.10.1");
    REQUIRE_FALSE(Vulkan::Ngx::RuntimeVersion("libnvidia-ngx-dlss.so."));
    REQUIRE_FALSE(Vulkan::Ngx::RuntimeVersion("libnvidia-ngx-dlss.so.1."));
    REQUIRE_FALSE(Vulkan::Ngx::RuntimeVersion("libnvidia-ngx-dlss.so.-1"));
    REQUIRE_FALSE(Vulkan::Ngx::RuntimeVersion("libnvidia-ngx-dlss.so.99999999999999999999999"));
    REQUIRE(Vulkan::Ngx::FindRuntimeInDirectory(root, true).empty());
    touch("libnvidia-ngx-dlssg.so.9.9");
    touch("libnvidia-ngx-dlssg.so.10.1");
    touch("libnvidia-ngx-dlssg.so.999.backup");
    REQUIRE(Vulkan::Ngx::FindRuntimeInDirectory(root, true).filename() ==
            "libnvidia-ngx-dlssg.so.10.1");
    touch("libnvidia-ngx-dlssg.so");
    REQUIRE(Vulkan::Ngx::FindRuntimeInDirectory(root, true).filename() ==
            "libnvidia-ngx-dlssg.so");
}
