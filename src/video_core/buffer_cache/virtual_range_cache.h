// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <boost/container/small_vector.hpp>

#include "common/common_types.h"
#include "common/container/unordered_map.h"
#include "video_core/memory_manager.h"

namespace VideoCommon {

struct VirtualSegment {
    GPUVAddr gpu_addr;
    DAddr device_addr;
    u32 size;
};

using VirtualSegments = boost::container::small_vector<VirtualSegment, 8>;

class VirtualRangeCache {
public:
    static constexpr size_t MAX_ENTRIES = 8192;
    static constexpr size_t MAX_DEFERRED = 4096;

    /// The segments of a range when it has more than one. Fewer than two means that it needs no
    /// splitting: one segment may then cover more than the range asked for.
    template <typename Memory = Tegra::MemoryManager>
    const VirtualSegments* Query(Memory& memory, GPUVAddr gpu_addr, u32 size) {
        if (has_deferred.load(std::memory_order_acquire)) {
            ApplyDeferred();
        }
        if (entries.size() > MAX_ENTRIES) {
            entries.clear();
        }
        if (size == 0 || gpu_addr > (std::numeric_limits<u64>::max)() - size) {
            span_result.clear();
            return &span_result;
        }
        const size_t as_id = memory.GetID();
        if (const auto address = FindSpan(as_id, gpu_addr, size)) {
            span_result.clear();
            span_result.push_back({gpu_addr, *address, size});
            return &span_result;
        }
        const u64 key = MakeKey(as_id, gpu_addr);
        const auto it = entries.find(key);
        if (it != entries.end() && it->second.as_id == as_id && it->second.gpu_addr == gpu_addr) {
            const Entry& cached = it->second;
            // Titles bind one address with several sizes (Ocarina of Time ~2,000 times a second,
            // half of them over 1 MiB). Keep the page table walk of the longest: its prefixes are
            // contiguous too.
            if (cached.size == size || (cached.segments.size() == 1 && size < cached.size)) {
                return &cached.segments;
            }
        }
        Entry entry{};
        entry.as_id = as_id;
        entry.gpu_addr = gpu_addr;
        entry.size = size;
        const auto ranges = memory.GetSubmappedRange(gpu_addr, size);
        GPUVAddr expected = gpu_addr;
        bool contiguous = true;
        for (const auto& [range_addr, range_size] : ranges) {
            if (range_addr != expected || range_size == 0) {
                contiguous = false;
                break;
            }
            const std::optional<DAddr> device_addr = memory.GpuToCpuAddress(range_addr);
            if (!device_addr || *device_addr == 0) {
                contiguous = false;
                break;
            }
            if (range_size > static_cast<size_t>((std::numeric_limits<u32>::max)())) {
                contiguous = false;
                break;
            }
            entry.segments.push_back(VirtualSegment{
                .gpu_addr = range_addr,
                .device_addr = *device_addr,
                .size = static_cast<u32>(range_size),
            });
            if (range_size > gpu_addr + size - expected) {
                contiguous = false;
                break;
            }
            expected += range_size;
        }
        if (!contiguous || expected != gpu_addr + size) {
            entry.segments.clear();
        }
        if (IsLinear(entry.segments, gpu_addr, size)) {
            AddSpan(as_id, gpu_addr, size, entry.segments.front().device_addr);
        }
        const auto result = entries.insert_or_assign(key, std::move(entry));
        return &result.first->second.segments;
    }

    void Unmap(size_t as_id, GPUVAddr gpu_addr, u64 size) {
        if (size == 0) {
            return;
        }
        {
            std::scoped_lock lock{deferred_mutex};
            if (size > (std::numeric_limits<u64>::max)() - gpu_addr) {
                deferred.clear();
                deferred_overflow = true;
                has_deferred.store(true, std::memory_order_release);
                return;
            }
            if (!deferred.empty()) {
                DeferredUnmap& last = deferred.back();
                if (last.as_id == as_id && last.gpu_addr + last.size == gpu_addr) {
                    last.size += size;
                    has_deferred.store(true, std::memory_order_release);
                    return;
                }
            }
            if (deferred.size() >= MAX_DEFERRED) {
                deferred.clear();
                deferred_overflow = true;
            } else {
                deferred.push_back(DeferredUnmap{
                    .as_id = as_id,
                    .gpu_addr = gpu_addr,
                    .size = size,
                });
            }
        }
        has_deferred.store(true, std::memory_order_release);
    }

private:
    struct Entry {
        VirtualSegments segments;
        size_t as_id{};
        GPUVAddr gpu_addr{};
        u32 size{};
    };

    struct DeferredUnmap {
        size_t as_id;
        GPUVAddr gpu_addr;
        u64 size;
    };

    struct Span {
        GPUVAddr end;
        DAddr device_addr;
    };
    using SpanKey = std::pair<size_t, GPUVAddr>;

    static bool IsLinear(const VirtualSegments& segments, GPUVAddr start, u32 size) {
        if (segments.empty() ||
            segments.front().device_addr > (std::numeric_limits<u64>::max)() - size) {
            return false;
        }
        GPUVAddr expected = start;
        const DAddr device = segments.front().device_addr;
        for (const auto& segment : segments) {
            if (segment.gpu_addr != expected ||
                segment.device_addr != device + (expected - start)) {
                return false;
            }
            expected += segment.size;
        }
        return expected == start + size;
    }

    std::optional<DAddr> FindSpan(size_t as_id, GPUVAddr address, u32 size) const {
        auto it = spans.upper_bound({as_id, address});
        if (it == spans.begin()) {
            return std::nullopt;
        }
        --it;
        if (it->first.first != as_id || it->second.end < address + size) {
            return std::nullopt;
        }
        return it->second.device_addr + (address - it->first.second);
    }

    void AddSpan(size_t as_id, GPUVAddr address, u32 size, DAddr device) {
        if (spans.size() >= MAX_ENTRIES) {
            spans.clear();
        }
        GPUVAddr start = address;
        GPUVAddr end = address + size;
        DAddr first_device = device;
        const auto same_offset = [&](const auto& entry) {
            return entry.second.device_addr - entry.first.second == device - address;
        };
        auto it = spans.lower_bound({as_id, start});
        if (it != spans.begin()) {
            const auto previous = std::prev(it);
            if (previous->first.first == as_id && previous->second.end >= start &&
                same_offset(*previous)) {
                it = previous;
            }
        }
        while (it != spans.end() && it->first.first == as_id && it->first.second <= end &&
               same_offset(*it)) {
            if (it->first.second < start) {
                start = it->first.second;
                first_device = it->second.device_addr;
            }
            end = (std::max)(end, it->second.end);
            it = spans.erase(it);
        }
        spans.insert_or_assign({as_id, start}, Span{end, first_device});
    }

    static u64 MakeKey(size_t as_id, GPUVAddr gpu_addr) {
        return (static_cast<u64>(as_id) << 48) ^ gpu_addr;
    }

    void ApplyDeferred() {
        std::vector<DeferredUnmap> pending;
        bool overflow = false;
        {
            std::scoped_lock lock{deferred_mutex};
            has_deferred.store(false, std::memory_order_release);
            pending.swap(deferred);
            overflow = deferred_overflow;
            deferred_overflow = false;
        }
        if (overflow) {
            entries.clear();
            spans.clear();
            return;
        }
        for (auto it = spans.begin(); it != spans.end();) {
            bool overlaps = false;
            for (const auto& unmap : pending) {
                if (unmap.as_id == it->first.first && it->first.second < unmap.gpu_addr + unmap.size &&
                    unmap.gpu_addr < it->second.end) {
                    overlaps = true;
                    break;
                }
            }
            it = overlaps ? spans.erase(it) : std::next(it);
        }
        if (pending.empty() || entries.empty()) {
            return;
        }
        for (auto it = entries.begin(); it != entries.end();) {
            const Entry& entry = it->second;
            const GPUVAddr entry_end = entry.gpu_addr + entry.size;
            bool overlaps = false;
            for (const DeferredUnmap& unmap : pending) {
                if (unmap.as_id != entry.as_id) {
                    continue;
                }
                if (entry.gpu_addr < unmap.gpu_addr + unmap.size && unmap.gpu_addr < entry_end) {
                    overlaps = true;
                    break;
                }
            }
            if (overlaps) {
                it = entries.erase(it);
            } else {
                ++it;
            }
        }
    }

    ::Common::unordered_map<u64, Entry> entries;
    std::map<SpanKey, Span> spans;
    VirtualSegments span_result;
    std::vector<DeferredUnmap> deferred;
    std::mutex deferred_mutex;
    std::atomic<bool> has_deferred{false};
    bool deferred_overflow{};
};

} // namespace VideoCommon
