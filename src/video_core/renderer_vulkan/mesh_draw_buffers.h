// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <utility>
#include <boost/container/static_vector.hpp>

#include "common/common_types.h"

namespace Vulkan {

/// Configure may submit between allocation and recording the draw that uses these buffers.
template <class Ref>
class MeshDrawBuffers {
public:
    template <class Pool, class Usage>
    Ref Request(Pool& pool, u64 size, Usage usage) {
        Ref ref = pool.Request(size, usage, true);
        buffers.push_back(ref);
        return ref;
    }

    void UseArena() {
        arena_used = true;
    }

    template <class Pool, class RetickArena>
    void Release(Pool& pool, u64 tick, RetickArena&& retick_arena) {
        for (Ref& ref : buffers) {
            pool.FreeDeferred(ref);
        }
        buffers.clear();
        if (std::exchange(arena_used, false)) {
            retick_arena(tick);
        }
    }

private:
    boost::container::static_vector<Ref, 4> buffers;
    bool arena_used{};
};

} // namespace Vulkan
