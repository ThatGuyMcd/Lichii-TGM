// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>
#include <limits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "video_core/buffer_cache/virtual_range_cache.h"

namespace {

using VideoCommon::VirtualRangeCache;

constexpr GPUVAddr BASE = 0x100000;
constexpr GPUVAddr HOLE = 0x10000;
constexpr DAddr JUMP = 0x1000000;

/// GPU memory that is contiguous, except for a device address jump HOLE into a split range.
struct FakeMemory {
    bool split{};
    mutable int walks{};
    size_t id{1};

    size_t GetID() const {
        return id;
    }

    std::vector<std::pair<GPUVAddr, size_t>> GetSubmappedRange(GPUVAddr gpu_addr,
                                                               size_t size) const {
        ++walks;
        if (split && gpu_addr < BASE + HOLE && gpu_addr + size > BASE + HOLE) {
            const size_t first = BASE + HOLE - gpu_addr;
            return {{gpu_addr, first}, {BASE + HOLE, size - first}};
        }
        return {{gpu_addr, size}};
    }

    std::optional<DAddr> GpuToCpuAddress(GPUVAddr gpu_addr) const {
        return DAddr{gpu_addr + 0x1000 + (split && gpu_addr >= BASE + HOLE ? JUMP : 0)};
    }
};

} // Anonymous namespace

TEST_CASE("VirtualRangeCache walks a contiguous address once for its longest size",
          "[video_core][virtual_range_cache]") {
    FakeMemory memory;
    VirtualRangeCache cache;
    REQUIRE(cache.Query(memory, BASE, 0x4000)->size() == 1);
    REQUIRE(memory.walks == 1);
    // Longer: walked again, and kept.
    REQUIRE(cache.Query(memory, BASE, 0x200000)->size() == 1);
    REQUIRE(memory.walks == 2);
    // Alternating sizes up to the longest walk nothing.
    for (int i = 0; i < 16; ++i) {
        REQUIRE(cache.Query(memory, BASE, 0x4000)->size() == 1);
        REQUIRE(cache.Query(memory, BASE, 0x200000)->size() == 1);
        REQUIRE(cache.Query(memory, BASE, 0x1000)->size() == 1);
    }
    REQUIRE(memory.walks == 2);
}

TEST_CASE("VirtualRangeCache walks again for ranges of a split address",
          "[video_core][virtual_range_cache]") {
    FakeMemory memory{.split = true};
    VirtualRangeCache cache;
    REQUIRE(cache.Query(memory, BASE, HOLE * 2)->size() == 2);
    // A prefix of a split range may be whole: the list of the longer range doesn't answer it.
    REQUIRE(cache.Query(memory, BASE, HOLE / 2)->size() == 1);
    REQUIRE(memory.walks == 2);
    REQUIRE(cache.Query(memory, BASE, HOLE * 2)->size() == 2);
    REQUIRE(memory.walks == 3);
}

TEST_CASE("VirtualRangeCache forgets a kept range when its tail is unmapped",
          "[video_core][virtual_range_cache]") {
    FakeMemory memory;
    VirtualRangeCache cache;
    REQUIRE(cache.Query(memory, BASE, 0x200000)->size() == 1);
    cache.Unmap(memory.GetID(), BASE + 0x100000, 0x1000);
    memory.split = true;
    REQUIRE(cache.Query(memory, BASE, HOLE * 2)->size() == 2);
    REQUIRE(memory.walks == 2);
}

TEST_CASE("Linear mapping spans answer shifting bindings and merge adjacent walks",
          "[video_core][virtual_range_cache]") {
    FakeMemory memory;
    VirtualRangeCache cache;
    REQUIRE(cache.Query(memory, BASE, HOLE)->size() == 1);
    REQUIRE(cache.Query(memory, BASE + HOLE, HOLE)->size() == 1);
    REQUIRE(memory.walks == 2);
    const auto* result = cache.Query(memory, BASE + 0x8000, HOLE);
    REQUIRE(result->size() == 1);
    REQUIRE(result->front().device_addr == BASE + 0x9000);
    REQUIRE(result->front().size == HOLE);
    REQUIRE(memory.walks == 2);
    memory.id = 2;
    REQUIRE(cache.Query(memory, BASE + 0x8000, HOLE)->size() == 1);
    REQUIRE(memory.walks == 3);
    memory.id = 1;
    memory.split = true;
    cache.Unmap(1, BASE + HOLE, HOLE);
    result = cache.Query(memory, BASE + 0x8000, HOLE);
    REQUIRE(result->size() == 2);
    REQUIRE(result->back().device_addr == BASE + HOLE + 0x1000 + JUMP);
    REQUIRE(memory.walks == 4);
}

TEST_CASE("Discontinuous mapping spans cannot merge across a device jump",
          "[video_core][virtual_range_cache]") {
    FakeMemory memory{.split = true};
    VirtualRangeCache cache;
    REQUIRE(cache.Query(memory, BASE, HOLE)->size() == 1);
    REQUIRE(cache.Query(memory, BASE + HOLE, HOLE)->size() == 1);
    REQUIRE(cache.Query(memory, BASE, HOLE * 2)->size() == 2);
    REQUIRE(memory.walks == 3);
}

TEST_CASE("Mapping span invalidation handles overflow and excessive deferred unmaps",
          "[video_core][virtual_range_cache]") {
    FakeMemory memory;
    VirtualRangeCache cache;
    REQUIRE(cache.Query(memory, BASE, HOLE)->size() == 1);
    REQUIRE(cache.Query(memory, (std::numeric_limits<u64>::max)() - 3, 8)->empty());
    REQUIRE(cache.Query(memory, BASE, 0)->empty());
    REQUIRE(memory.walks == 1);
    cache.Unmap(1, (std::numeric_limits<u64>::max)() - 3, 8);
    REQUIRE(cache.Query(memory, BASE + 0x1000, 0x1000)->size() == 1);
    REQUIRE(memory.walks == 2);
    REQUIRE(cache.Query(memory, BASE, HOLE)->size() == 1);
    for (size_t i = 0; i <= VirtualRangeCache::MAX_DEFERRED; ++i) {
        cache.Unmap(1, 0x10000000 + i * 0x2000, 0x1000);
    }
    const int before = memory.walks;
    REQUIRE(cache.Query(memory, BASE + 0x1000, 0x1000)->size() == 1);
    REQUIRE(memory.walks == before + 1);
}
