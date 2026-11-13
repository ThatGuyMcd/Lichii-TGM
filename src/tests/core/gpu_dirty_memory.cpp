// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <functional>
#include <set>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/gpu_dirty_memory_manager.h"

TEST_CASE("GPUDirtyMemoryManager reports collected writes until they are gathered",
          "[core][gpu_dirty_memory]") {
    Core::GPUDirtyMemoryManager manager;
    REQUIRE_FALSE(manager.MayHavePending());
    manager.Collect(0x10040, 0x10);
    manager.Collect(0x20000, 0x40);
    REQUIRE(manager.MayHavePending());

    REQUIRE(manager.Overlaps(0x10040, 1));
    REQUIRE(manager.Overlaps(0x10000, 0x1000));
    REQUIRE(manager.Overlaps(0x20000, 4));
    REQUIRE_FALSE(manager.Overlaps(0x10000, 0x40));
    REQUIRE_FALSE(manager.Overlaps(0x10080, 0x40));
    REQUIRE_FALSE(manager.Overlaps(0x30000, 0x1000));

    std::vector<std::pair<PAddr, size_t>> gathered;
    std::function<void(PAddr, size_t)> callback = [&](PAddr address, size_t size) {
        gathered.emplace_back(address, size);
    };
    manager.Gather(callback);
    REQUIRE_FALSE(manager.MayHavePending());

    REQUIRE(gathered.size() == 2);
    REQUIRE_FALSE(manager.Overlaps(0x10000, 0x1000));
    REQUIRE_FALSE(manager.Overlaps(0x20000, 4));
}

TEST_CASE("GPU dirty uploads crossing half-pages retain every touched cache line",
          "[core][gpu_dirty_memory]") {
    Core::GPUDirtyMemoryManager manager;
    std::set<PAddr> expected;
    const auto collect = [&](PAddr address, size_t size) {
        manager.Collect(address, size);
        if (size == 0) {
            return;
        }
        for (PAddr line = address & ~PAddr{63}; line < address + size; line += 64) {
            expected.insert(line);
        }
    };
    collect(0x107F1, 0x1812);
    collect(0x13040, 0x40);
    collect(0x14000, 0);
    collect(0x10800, 0x800);

    std::set<PAddr> pending;
    std::function<void(PAddr, size_t)> callback = [&](PAddr address, size_t size) {
        REQUIRE(address % 64 == 0);
        REQUIRE(size % 64 == 0);
        for (PAddr line = address; line < address + size; line += 64) {
            pending.insert(line);
        }
    };
    manager.ForEachPending(callback);
    REQUIRE(pending == expected);
    for (const PAddr line : expected) {
        CHECK(manager.Overlaps(line, 1));
    }
    CHECK_FALSE(manager.Overlaps(0x13000, 64));
    CHECK_FALSE(manager.Overlaps(0x14000, 64));

    pending.clear();
    manager.Gather(callback);
    REQUIRE(pending == expected);
    pending.clear();
    manager.ForEachPending(callback);
    CHECK(pending.empty());
}
