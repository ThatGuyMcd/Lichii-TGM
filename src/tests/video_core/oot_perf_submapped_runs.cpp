// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>
#include <random>
#include <chrono>
#include <iostream>
#include <catch2/catch_test_macros.hpp>
#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "tests/video_core/oot_perf_null_rasterizer.h"
#include "video_core/memory_manager.h"

TEST_CASE("Submapped run snapshots agree with page walks through mapping churn",
          "[video_core][oot_perf][memory_manager]") {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    OotPerfNullRasterizer rasterizer;
    Tegra::MemoryManager memory{system, device_memory, 40, 1ULL << 34, 16};
    memory.BindRasterizer(&rasterizer);
    constexpr GPUVAddr base = 0x2100000000ULL;
    constexpr DAddr dev = 0x100000;
    constexpr u64 page = 0x1000;
    constexpr u64 big = 0x10000;
    std::mt19937_64 random{0x1820};
    const auto compare = [&] {
        for (size_t i = 0; i < 96; ++i) {
            const auto addr = base - page + random() % (big * 5);
            const auto size = random() % (big * 2);
            REQUIRE(memory.GetSubmappedRange(addr, size) ==
                    memory.GetSubmappedRangeUncached(addr, size));
            REQUIRE(memory.GetSubmappedDeviceRange(addr, size) ==
                    memory.GetSubmappedDeviceRangeUncached(addr, size));
        }
    };
    memory.Map(base, dev, big * 2, Tegra::PTEKind::PITCH, true);
    memory.Map(base + big * 3, dev + big * 8, big, Tegra::PTEKind::PITCH, false);
    compare();
    // Same GPU range, new backing: cached snapshots must publish the new device addresses.
    memory.Map(base, dev + big * 12, big, Tegra::PTEKind::PITCH, true);
    compare();
    memory.Unmap(base + big * 3 + page * 2, page);
    memory.Map(base + big * 3 + page * 4, dev + big * 16, page,
               Tegra::PTEKind::PITCH, false);
    compare();
    memory.Unmap(base, big * 2);
    compare();

    constexpr GPUVAddr end = 1ULL << 40;
    memory.Map(end - page, dev, page, Tegra::PTEKind::PITCH, false);
    REQUIRE(memory.GetSubmappedRange(end - 1, 1) == memory.GetSubmappedRangeUncached(end - 1, 1));
    REQUIRE(memory.GetSubmappedRange(end - 1, 2).empty());
    REQUIRE(memory.GetSubmappedRange(end, 0).empty());
    REQUIRE(memory.GetSubmappedRange(std::numeric_limits<u64>::max(), 2).empty());
    REQUIRE(memory.GetSubmappedDeviceRange(end - page, std::numeric_limits<u64>::max()) ==
            memory.GetSubmappedDeviceRangeUncached(end - page, std::numeric_limits<u64>::max()));
    REQUIRE(memory.GetSubmappedDeviceRange(end, 1).empty());
}

TEST_CASE("Submapped run query CPU benchmark", "[.][oot_perf_mapping_benchmark]") {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    OotPerfNullRasterizer rasterizer;
    Tegra::MemoryManager memory{system, device_memory, 40, 1ULL << 34, 16};
    memory.BindRasterizer(&rasterizer);
    constexpr GPUVAddr base = 0x2100000000ULL;
    constexpr DAddr dev = 0x100000;
    constexpr u64 page = 0x1000;
    constexpr u64 extent = 4 * 1024 * 1024;
    constexpr size_t repetitions = 256;
    size_t checksum{};
    for (const bool fragmented : {false, true}) {
        memory.Map(base, dev, extent, Tegra::PTEKind::PITCH, false);
        if (fragmented) {
            for (u64 offset = page; offset < extent; offset += page * 2) {
                memory.Unmap(base + offset, page);
            }
        }
        for (const u64 size : {u64{64 * 1024}, extent}) {
            const auto expected = memory.GetSubmappedDeviceRangeUncached(base, size);
            REQUIRE(memory.GetSubmappedDeviceRange(base, size) == expected);
            // Warm the immutable snapshot and allocator paths before alternating timing order.
            for (size_t warm = 0; warm < 4; ++warm) {
                checksum += memory.GetSubmappedDeviceRange(base, size).size();
                checksum += memory.GetSubmappedDeviceRangeUncached(base, size).size();
            }
            double run_ns{}, page_ns{};
            const auto measure = [&](bool runs) {
                const auto start = std::chrono::steady_clock::now();
                for (size_t iteration = 0; iteration < repetitions; ++iteration) {
                    const auto result = runs ? memory.GetSubmappedDeviceRange(base, size)
                                             : memory.GetSubmappedDeviceRangeUncached(base, size);
                    checksum += result.size();
                }
                return std::chrono::duration<double, std::nano>(
                           std::chrono::steady_clock::now() - start).count();
            };
            for (size_t round = 0; round < 8; ++round) {
                if (round % 2 == 0) {
                    run_ns += measure(true);
                    page_ns += measure(false);
                } else {
                    page_ns += measure(false);
                    run_ns += measure(true);
                }
                REQUIRE(memory.GetSubmappedDeviceRange(base, size) == expected);
            }
            std::cout << "mapping-query fragmented=" << fragmented << " bytes=" << size
                      << " repetitions=" << repetitions * 8 << " runs_ns="
                      << run_ns / (repetitions * 8) << " page_walk_ns="
                      << page_ns / (repetitions * 8) << " ranges=" << expected.size() << '\n';
        }
        memory.Unmap(base, extent);
    }
    REQUIRE(checksum != 0);
}
