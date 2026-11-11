// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"

namespace Vulkan {

/// Work belongs to the current submission even before a batched upload becomes a command.
class PendingSubmission {
public:
    void Record() noexcept {
        ++recorded;
    }

    void Submitted() noexcept {
        submitted = recorded;
    }

    void QueueUploads() noexcept {
        uploads_pending = true;
    }

    void DrainUploads() noexcept {
        uploads_pending = false;
    }

    void QueueReorderedUploads() noexcept {
        reordered_uploads_pending = true;
    }

    void DrainReorderedUploads() noexcept {
        reordered_uploads_pending = false;
    }

    [[nodiscard]] bool UploadsPending() const noexcept {
        return uploads_pending;
    }

    [[nodiscard]] bool ReorderedUploadsPending() const noexcept {
        return reordered_uploads_pending;
    }

    [[nodiscard]] bool HasWork(bool deferred_clear = false) const noexcept {
        return recorded != submitted || uploads_pending || reordered_uploads_pending ||
               deferred_clear;
    }

private:
    u64 recorded{};
    u64 submitted{};
    bool uploads_pending{};
    bool reordered_uploads_pending{};
};

} // namespace Vulkan
