// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include "common/fs/path_util.h"
#include "core/crypto/key_manager.h"

// Prevent the first KeyManager construction in a parser test from reading installed keys.
// ReloadKeys does not clear process state. Run missing-key and synthetic-key groups separately.
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

    Core::Crypto::Key256 InstallSyntheticHeaderKey() const {
        // XTS rejects identical key halves. These generated test bytes are not game keys.
        Core::Crypto::Key256 key{};
        const auto file = directory / "prod.keys";
        {
            std::ofstream output(file);
            output << "header_key = " << std::hex << std::setfill('0');
            for (size_t i = 0; i < key.size(); ++i) {
                key[i] = static_cast<u8>(i + 1);
                output << std::setw(2) << static_cast<unsigned>(key[i]);
            }
            output << '\n';
            if (!output) {
                throw std::runtime_error("Cannot write synthetic parser header key");
            }
        }
        Core::Crypto::KeyManager::Instance().ReloadKeys();
        std::filesystem::remove(file);
        return key;
    }

private:
    const std::filesystem::path previous =
        Common::FS::GetEdenPath(Common::FS::EdenPath::KeysDir);
    const std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("lichii-parser-keys-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
};
