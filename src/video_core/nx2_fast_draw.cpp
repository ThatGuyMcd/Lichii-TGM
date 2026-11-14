// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>

#include "video_core/memory_manager.h"
#include "video_core/nx2.h"
#include "video_core/nx2_fast_draw.h"

namespace VideoCommon::Nx2FastDraw {

bool DiagnoseConstantWrite(const Tegra::MemoryManager& memory, GPUVAddr begin, const void* words,
                           u64 bytes) {
    auto& stats = t_constant_write_diagnostics;
    ++stats.writes;
    stats.bytes += bytes;
    constexpr u64 PageSize = 0x1000;
    if (words == nullptr || bytes == 0 || begin + bytes < begin) {
        ++stats.unavailable;
        return false;
    }
    if (bytes > PageSize - (begin & (PageSize - 1))) {
        ++stats.cross_page;
        return false;
    }
    if (begin + bytes > Tegra::g_nx2_sparse_low.load(std::memory_order_relaxed) &&
        begin < Tegra::g_nx2_sparse_high.load(std::memory_order_relaxed)) {
        ++stats.sparse;
        return false;
    }
    if (!Tegra::g_nx2_ampere.load(std::memory_order_relaxed)) {
        ++stats.non_native;
        return false;
    }
    const auto device = memory.GpuToCpuAddress(begin);
    const u8* const held = memory.GetSpan(begin, bytes);
    if (!device || held == nullptr) {
        ++stats.unavailable;
        return false;
    }
    if (Tegra::Nx2MayBeGpuWritten(*device, bytes)) {
        ++stats.may_gpu_written;
        return false;
    }
    // Reports can begin before these bytes and span at most 16 bytes. Inspect their preceding
    // slot too; slot hashes can conservatively report an unrelated pending report.
    const DAddr report_begin = *device >= 15 ? *device - 15 : 0;
    if (!Tegra::Nx2AsyncReportsSettled(report_begin, bytes + (*device - report_begin))) {
        ++stats.pending_report;
        return false;
    }
    if (std::memcmp(held, words, static_cast<size_t>(bytes)) == 0) {
        ++stats.equal_writes;
        stats.equal_bytes += bytes;
        return true;
    }
    ++stats.changed_writes;
    stats.changed_bytes += bytes;
    return false;
}

void DeviceSpans::Update(const Tegra::MemoryManager& memory_, GPUVAddr address_, u64 size_) {
    const u64 serial = memory_.MapSerial();
    if (memory == &memory_ && address == address_ && size == size_ && map_serial == serial) {
        return;
    }
    memory = &memory_;
    address = address_;
    size = size_;
    map_serial = serial;
    spans.clear();
    const auto submapped = memory_.GetSubmappedDeviceRange(address_, size_);
    // Segments come in GPU order, joined where device memory continues.
    linear = submapped.size() == 1 && submapped[0].second == size_;
    linear_base = linear ? submapped[0].first : 0;
    for (const auto& [device, bytes] : submapped) {
        spans.emplace_back(device, device + bytes);
    }
    std::ranges::sort(spans);
    size_t merged = 0;
    for (const auto& span : spans) {
        if (merged != 0 && span.first <= spans[merged - 1].second) {
            spans[merged - 1].second = (std::max)(spans[merged - 1].second, span.second);
        } else {
            spans[merged++] = span;
        }
    }
    spans.resize(merged);
}

bool DeviceSpans::Intersects(DAddr begin, DAddr end) const {
    const auto span = std::ranges::upper_bound(spans, begin, {}, &std::pair<DAddr, DAddr>::second);
    return span != spans.end() && span->first < end;
}

} // namespace VideoCommon::Nx2FastDraw
