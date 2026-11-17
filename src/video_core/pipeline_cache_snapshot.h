// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <system_error>
#include "common/fs/file.h"
#include "common/fs/file_lock.h"

namespace VideoCommon {

/// Publishes only a complete, committed snapshot. A failed write keeps the previous cache.
template <typename Write>
bool ReplacePipelineCacheSnapshot(const std::filesystem::path& file, Write&& write) {
    auto lock_path = file;
    lock_path += ".lock";
    const Common::FS::FileLock lock{lock_path};
    if (!lock.Held()) {
        return false;
    }
    auto temporary = file;
    temporary += ".tmp";
    bool complete = false;
    {
        const Common::FS::IOFile output{temporary, Common::FS::FileAccessMode::Write,
                                        Common::FS::FileType::BinaryFile};
        if (!output.IsOpen()) {
            return false;
        }
        complete = write(output) && output.Commit();
    }
    std::error_code error;
    if (complete) {
        std::filesystem::rename(temporary, file, error);
        if (!error) {
            return true;
        }
    }
    std::filesystem::remove(temporary, error);
    return false;
}

} // namespace VideoCommon
