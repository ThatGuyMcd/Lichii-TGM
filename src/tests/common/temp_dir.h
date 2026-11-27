// SPDX-FileCopyrightText: Copyright 2026 Lichii Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace Tests {

class TempDir {
public:
    explicit TempDir(std::string_view prefix) {
        const auto parent = std::filesystem::temp_directory_path();
        std::random_device random;
        for (int attempt = 0; attempt < 32; ++attempt) {
            path = parent / (std::string{prefix} + '-' + std::to_string(random()));
            if (std::filesystem::create_directory(path)) {
                return;
            }
        }
        throw std::runtime_error("Cannot create a unique test directory");
    }

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& Path() const {
        return path;
    }

private:
    std::filesystem::path path;
};

} // namespace Tests
