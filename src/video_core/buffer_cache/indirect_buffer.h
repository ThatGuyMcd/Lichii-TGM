// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "video_core/buffer_cache/buffer_cache_base.h"

namespace VideoCommon {
// Dispatch arguments are three GPU-readable words. Keep the producer's pending downloads:
// discarding them would let a later CPU-dirty page upload replace them with stale bytes.
// Mark the read so upload reordering cannot move a later write before this dispatch.
template <class Cache>
auto ObtainIndirectBuffer(Cache& cache, GPUVAddr address) {
    auto result = cache.ObtainBuffer(address, 12, ObtainBufferSynchronize::FullSynchronize,
                                     ObtainBufferOperation::DoNothing);
    result.first->MarkUsage(result.second, 12);
    return result;
}
} // namespace VideoCommon
