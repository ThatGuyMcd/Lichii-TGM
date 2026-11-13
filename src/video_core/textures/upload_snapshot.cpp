// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <cstring>

#include "common/assert.h"
#include "common/scope_exit.h"
#include "video_core/memory_manager.h"
#include "video_core/textures/upload_snapshot.h"
#include "video_core/textures/workers.h"

namespace VideoCommon {
namespace {
std::atomic<size_t> snapshot_bytes{};
}

TextureUploadSnapshot::TextureUploadSnapshot(const ImageInfo& info_, bool native_,
                                             size_t guest_size, size_t host_size)
    : info{info_}, native{native_}, charged_bytes{guest_size + host_size}, input(guest_size),
      output(host_size), copies{UnswizzleImageCopies(info)} {}

TextureUploadSnapshot::~TextureUploadSnapshot() {
    snapshot_bytes.fetch_sub(charged_bytes, std::memory_order_release);
}

std::shared_ptr<TextureUploadSnapshot> TextureUploadSnapshot::Capture(
    Tegra::MemoryManager& memory, GPUVAddr address, const ImageInfo& info,
    Common::ThreadWorker* workers) {
    if (info.type == ImageType::Linear || info.type == ImageType::Buffer) {
        return {};
    }
    const size_t guest_size = CalculateGuestSizeInBytes(info);
    const size_t host_size = CalculateUnswizzledSizeBytes(info);
    if (guest_size < MinBytes || guest_size > MaxBytes || host_size == 0 || host_size > MaxBytes) {
        return {};
    }
    const size_t charge = guest_size + host_size;
    size_t used = snapshot_bytes.load(std::memory_order_relaxed);
    do {
        if (used > BudgetBytes - charge) {
            return {};
        }
    } while (!snapshot_bytes.compare_exchange_weak(used, used + charge,
                                                   std::memory_order_acq_rel));
    bool owns_charge = false;
    SCOPE_EXIT {
        if (!owns_charge) {
            snapshot_bytes.fetch_sub(charge, std::memory_order_release);
        }
    };
    try {
        auto owned = std::unique_ptr<TextureUploadSnapshot>(
            new TextureUploadSnapshot(info, memory.UsesNativeTiling(), guest_size, host_size));
        owns_charge = true;
        auto snapshot = std::shared_ptr<TextureUploadSnapshot>(std::move(owned));
        // Matches the synchronous GpuGuestMemory<UnsafeRead> path, before later guest writes or
        // mapping changes. No MemoryManager, image, guest pointer or staging span reaches a worker.
        memory.ReadBlockUnsafe(address, snapshot->input.data(), guest_size);
        auto& pool = workers ? *workers : Tegra::Texture::GetThreadWorkers();
        // A full pool leaves the job for CopyTo to claim in command order.
        (void)pool.TryQueueWork([snapshot] { snapshot->Decode(); });
        return snapshot;
    } catch (const std::bad_alloc&) {
        return {}; // Keep the existing synchronous path under host memory pressure.
    }
}

void TextureUploadSnapshot::Decode() noexcept {
    std::call_once(decoded, [this] {
        try {
            UnswizzleImageSnapshot(info, input, output, native);
        } catch (...) {
            failure = std::current_exception();
        }
    });
}

void TextureUploadSnapshot::CopyTo(std::span<u8> staging) {
    Decode();
    if (failure) {
        std::rethrow_exception(failure);
    }
    ASSERT(staging.size_bytes() >= output.size());
    std::memcpy(staging.data(), output.data(), output.size());
}

} // namespace VideoCommon
