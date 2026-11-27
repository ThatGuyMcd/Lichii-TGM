// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <stdexcept>
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "tests/common/temp_dir.h"

class ScopedSystemTestContext {
public:
    ScopedSystemTestContext() {
        // SetEdenPath can restore only an existing directory on a fresh test profile.
        std::filesystem::create_directories(previous_nand);
        std::filesystem::create_directories(previous_sdmc);
        const auto sdmc = directory.Path() / "sdmc";
        if (!std::filesystem::create_directory(sdmc)) {
            throw std::runtime_error("Cannot create isolated System SD card directory");
        }
        Common::FS::SetEdenPath(Common::FS::EdenPath::NANDDir, directory.Path());
        Common::FS::SetEdenPath(Common::FS::EdenPath::SDMCDir, sdmc);
        Settings::values.memory_layout_mode.SetValue(Settings::MemoryLayout::Memory_4Gb);
        Settings::values.use_multi_core.SetValue(false);
    }

    ~ScopedSystemTestContext() {
        Common::FS::SetEdenPath(Common::FS::EdenPath::NANDDir, previous_nand);
        Common::FS::SetEdenPath(Common::FS::EdenPath::SDMCDir, previous_sdmc);
        Settings::values.current_user = previous_user;
        Settings::values.memory_layout_mode.SetValue(previous_memory);
        Settings::values.use_multi_core.SetValue(previous_multicore);
    }

    ScopedSystemTestContext(const ScopedSystemTestContext&) = delete;
    ScopedSystemTestContext& operator=(const ScopedSystemTestContext&) = delete;

private:
    const std::filesystem::path previous_nand =
        Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir);
    const std::filesystem::path previous_sdmc =
        Common::FS::GetEdenPath(Common::FS::EdenPath::SDMCDir);
    const s32 previous_user = Settings::values.current_user.GetValue();
    const Settings::MemoryLayout previous_memory = Settings::values.memory_layout_mode.GetValue();
    const bool previous_multicore = Settings::values.use_multi_core.GetValue();
    const Tests::TempDir directory{"hwacod-system-test"};
};
