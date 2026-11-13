// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <exception>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include "common/common_types.h"
#include "common/thread_worker.h"
#include "video_core/texture_cache/image_info.h"
#include "video_core/texture_cache/util.h"

namespace VideoCommon {

/// Guest bytes are captured on the owner; workers only decode that immutable snapshot. The
/// command recorder can claim a queued decode, so a busy transcode pool cannot starve submission.
class TextureUploadSnapshot {
public:
    static constexpr size_t MinBytes = 64 * 1024;
    static constexpr size_t MaxBytes = 8 * 1024 * 1024;
    static constexpr size_t BudgetBytes = 64 * 1024 * 1024;

    [[nodiscard]] static std::shared_ptr<TextureUploadSnapshot> Capture(
        Tegra::MemoryManager& memory, GPUVAddr address, const ImageInfo& info,
        Common::ThreadWorker* workers = nullptr);
    ~TextureUploadSnapshot();

    [[nodiscard]] std::span<const BufferImageCopy> Copies() const noexcept {
        return copies;
    }

    /// Called in command order, before any command reading the upload staging range.
    void CopyTo(std::span<u8> staging);

private:
    TextureUploadSnapshot(const ImageInfo& info_, bool native_, size_t guest_size,
                          size_t host_size);
    void Decode() noexcept;

    ImageInfo info;
    bool native;
    size_t charged_bytes;
    std::vector<u8> input;
    std::vector<u8> output;
    boost::container::small_vector<BufferImageCopy, 16> copies;
    std::once_flag decoded;
    std::exception_ptr failure;
};

} // namespace VideoCommon
