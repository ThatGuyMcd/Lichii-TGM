// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <iostream>
#include <string>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"

int main(int argc, char** argv) {
    if (argc != 3 || !std::filesystem::path{argv[1]}.is_absolute()) {
        return 2;
    }
    const std::filesystem::path directory{argv[1]};
    const bool created = !std::filesystem::exists(directory);
    const bool parent_existed = std::filesystem::exists(directory.parent_path());
    if (created && !std::filesystem::create_directories(directory)) {
        return 2;
    }
    if (!std::filesystem::is_directory(directory)) {
        return 2;
    }
    Common::FS::SetEdenPath(Common::FS::EdenPath::LogDir, directory);
    if (Common::FS::GetEdenPath(Common::FS::EdenPath::LogDir) != directory) {
        return 2;
    }
    // SetEdenPath accepts existing directories only. Remove only newly created, empty test
    // directories after binding, so the missing-parent case cannot fall back to owner paths.
    if (created && (!std::filesystem::remove(directory) ||
                    (!parent_existed && !std::filesystem::remove(directory.parent_path())))) {
        return 2;
    }
    Settings::values.censor_username.SetValue(false);
    Settings::values.log_flush_line.SetValue(true);
    Common::Log::Initialize();
    Common::Log::SetColorConsoleBackendEnabled(true);
    Common::Log::SetRingCapacity(8);
    const std::string marker = std::string{"independent-log-session-"} + argv[2];
    const auto write = [&](const std::string& command) {
        LOG_CRITICAL(Debug, "{} {}", marker, command);
        Common::Log::Stop();
        const auto ring = Common::Log::ReadRing(0);
        if (ring.empty() || ring.back().text.find(marker + " " + command) == std::string::npos) {
            return false;
        }
        std::cout << "READY " << argv[2] << ' ' << command << std::endl;
        return true;
    };
    if (!write("start")) {
        return 3;
    }
    for (std::string command; std::getline(std::cin, command);) {
        if (command == "exit") {
            return 0;
        }
        if (!write(command)) {
            return 4;
        }
    }
    return 0;
}
