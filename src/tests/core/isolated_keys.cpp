// SPDX-License-Identifier: GPL-3.0-or-later
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include "common/fs/path_util.h"

namespace {

class IsolatedKeys final : public Catch::EventListenerBase {
public:
    using EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override {
        previous = Common::FS::GetEdenPath(Common::FS::EdenPath::KeysDir);
        previous_log = Common::FS::GetEdenPath(Common::FS::EdenPath::LogDir);
        directory = std::filesystem::temp_directory_path() /
                    ("lichii-test-keys-" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(directory))
            throw std::runtime_error("Cannot create isolated test keys directory");
        Common::FS::SetEdenPath(Common::FS::EdenPath::KeysDir, directory);
        const auto logs = directory / "log";
        std::filesystem::create_directory(logs);
        Common::FS::SetEdenPath(Common::FS::EdenPath::LogDir, logs);
    }

    void testRunEnded(const Catch::TestRunStats&) override {
        if (std::filesystem::is_directory(previous))
            Common::FS::SetEdenPath(Common::FS::EdenPath::KeysDir, previous);
        if (std::filesystem::is_directory(previous_log))
            Common::FS::SetEdenPath(Common::FS::EdenPath::LogDir, previous_log);
        std::error_code error;
        const auto target = std::filesystem::weakly_canonical(directory, error);
        const auto parent = std::filesystem::weakly_canonical(
            std::filesystem::temp_directory_path(), error);
        if (error || target.parent_path() != parent ||
            !target.filename().string().starts_with("lichii-test-keys-"))
            throw std::runtime_error("Isolated keys cleanup escaped the temporary directory");
        std::filesystem::remove_all(directory, error);
    }

private:
    std::filesystem::path previous;
    std::filesystem::path previous_log;
    std::filesystem::path directory;
};

} // namespace

CATCH_REGISTER_LISTENER(IsolatedKeys)
