// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>
#include <filesystem>
#include <system_error>

#include "common/nvidia_shader_cache.h"

namespace Common {

// zortos293/Lichii #1301: keep large OOT caches separate from applications with a smaller limit.
void SetUpNvidiaShaderDiskCache() {
#if defined(__linux__) && !defined(__ANDROID__)
    if (std::getenv("__GL_SHADER_DISK_CACHE_PATH") == nullptr) {
        const char* const xdg_cache = std::getenv("XDG_CACHE_HOME");
        const char* const home = std::getenv("HOME");
        std::filesystem::path base;
        if (xdg_cache != nullptr && xdg_cache[0] == '/') {
            base = xdg_cache;
        } else if (home != nullptr && home[0] == '/') {
            base = std::filesystem::path{home} / ".cache";
        }
        if (!base.empty()) {
            const auto folder = base / "eden" / "nvidia";
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            if (!error) {
                setenv("__GL_SHADER_DISK_CACHE_PATH", folder.c_str(), 0);
            }
        }
    }
    setenv("__GL_SHADER_DISK_CACHE_SIZE", "10737418240", 0);
#endif
}

} // namespace Common
