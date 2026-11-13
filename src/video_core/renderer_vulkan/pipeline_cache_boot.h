// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include "common/fs/file_lock.h"
#include "common/fs/fs.h"

namespace Vulkan {

/// A failed boot must not immediately submit the same cached pipelines to the driver again.
/// Complete is deliberately explicit: unwinding an unsuccessful load keeps the marker.
class PipelineCacheBoot {
public:
    explicit PipelineCacheBoot(const std::filesystem::path& cache)
        : marker{MarkerPath(cache)}, lock{LockPath(marker)},
          build{lock.Held() ? !Common::FS::Exists(marker) : !lock.Busy()} {}

    bool BuildAtBoot() const {
        return build;
    }

    bool Busy() const {
        return lock.Busy();
    }

    bool Begin() {
        return !lock.Held() || Common::FS::NewFile(marker);
    }

    void Complete() {
        if (lock.Held()) {
            Common::FS::RemoveFile(marker);
        }
    }

    static std::filesystem::path MarkerPath(std::filesystem::path cache) {
        cache += ".booting";
        return cache;
    }

private:
    static std::filesystem::path LockPath(std::filesystem::path path) {
        path += ".lock";
        return path;
    }

    std::filesystem::path marker;
    Common::FS::FileLock lock;
    bool build;
};

} // namespace Vulkan
