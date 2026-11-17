// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include "common/fs/path_util.h"

// Prevent the first KeyManager construction in a parser test from reading installed keys.
// Already-loaded process state is left alone; tests explicitly refuse a present header key.
class ScopedParserKeysDirectory {
public:
    ScopedParserKeysDirectory() {
        if (!std::filesystem::create_directory(directory)) {
            throw std::runtime_error("Cannot create isolated parser keys directory");
        }
        Common::FS::SetEdenPath(Common::FS::EdenPath::KeysDir, directory);
    }
    ~ScopedParserKeysDirectory() {
        Common::FS::SetEdenPath(Common::FS::EdenPath::KeysDir, previous);
        std::error_code error;
        std::filesystem::remove(directory, error);
    }
    ScopedParserKeysDirectory(const ScopedParserKeysDirectory&) = delete;
    ScopedParserKeysDirectory& operator=(const ScopedParserKeysDirectory&) = delete;

private:
    const std::filesystem::path previous =
        Common::FS::GetEdenPath(Common::FS::EdenPath::KeysDir);
    const std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("lichii-parser-keys-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
};
