// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "common/assert.h"
#include "common/common_types.h"

namespace Vulkan {

/// Held downloads are excluded from reuse scans. Capacity is reserved before acquiring ownership,
/// so returning a held buffer during scope-exit cleanup cannot allocate or throw.
template <typename T, template <typename> class Allocator = std::allocator>
class DeferredBufferCache {
    static_assert(std::is_nothrow_move_constructible_v<T>);
    static_assert(std::is_nothrow_move_assignable_v<T>);

public:
    using EntriesType = std::vector<T, Allocator<T>>;

    EntriesType& Entries() noexcept { return entries; }
    const EntriesType& Entries() const noexcept { return entries; }
    size_t Size() const noexcept { return entries.size() + held.size(); }

    T& Add(T entry, bool deferred) {
        const size_t needed = Size() + 1;
        if (needed > entries.capacity()) {
            const size_t capacity = entries.capacity();
            const size_t growth = (std::min)(capacity / 2 + 1, entries.max_size() - capacity);
            entries.reserve((std::max)(needed, capacity + growth));
        }
        if (deferred) {
            auto [it, inserted] = held.try_emplace(entry.index, std::move(entry));
            ASSERT(inserted);
            return it->second;
        }
        return entries.emplace_back(std::move(entry));
    }

    T& Hold(size_t position) {
        ASSERT(position < entries.size());
        // Allocate the node before moving the live buffer. Failure leaves the reusable entry intact.
        auto [it, inserted] = held.try_emplace(entries[position].index);
        ASSERT(inserted);
        it->second = std::move(entries[position]);
        if (position + 1 != entries.size()) {
            entries[position] = std::move(entries.back());
        }
        entries.pop_back();
        return it->second;
    }

    T* FindHeld(u64 index) noexcept {
        const auto it = held.find(index);
        return it != held.end() ? &it->second : nullptr;
    }

    T& Release(u64 index) noexcept {
        auto node = held.extract(index);
        ASSERT(!node.empty());
        ASSERT(entries.size() < entries.capacity());
        return entries.emplace_back(std::move(node.mapped()));
    }

private:
    EntriesType entries;
    std::unordered_map<u64, T, std::hash<u64>, std::equal_to<u64>,
                       Allocator<std::pair<const u64, T>>> held;
};

} // namespace Vulkan
