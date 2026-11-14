// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>
#include <catch2/catch_test_macros.hpp>
#include "common/page_table.h"
#include "common/scope_exit.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/memory.h"
#include "tests/core/system_test_context.h"

TEST_CASE("Guest spans validate interior pages and exact exclusive bounds", "[hwacod_memory]") {
    ScopedSystemTestContext context;
    Core::System system;
    Core::Memory::Memory unbound{system};
    REQUIRE(unbound.GetSpan(0x4000, 4) == nullptr);
    system.Initialize();
    Kernel::KProcess process{system.Kernel()};
    SCOPE_EXIT { system.Kernel().UnregisterKernelObject(&process); };
    auto& table = process.GetPageTable().GetImpl();
    table.Resize(20, 12);
    u8* const backing = system.DeviceMemory().buffer.BackingBasePointer();
    const auto map = [&](u64 page, u16 block, u64 physical) {
        table.entries.GetAndFault(page).Store(false, Common::PageType::Memory, block,
            reinterpret_cast<uintptr_t>(backing + physical) - (page << 12));
    };
    auto& memory = process.GetMemory();
    memory.SetCurrentPageTable(process);
    map(4, 1, 0x10000);
    map(5, 1, 0x11000);
    map(6, 2, 0x40000);
    REQUIRE(memory.GetSpan(0x4000, 0x2000) == backing + 0x10000);
    REQUIRE(memory.GetSpan(0x4800, 0x1800) == backing + 0x10800);
    REQUIRE(memory.GetSpan(0x4000, 0x2001) == nullptr);
    REQUIRE(memory.GetSpan(0x4000, 0) == backing + 0x10000);
    REQUIRE(memory.GetSpan(0x7000, 0) == nullptr);
    map(8, 3, 0x20000);
    map(9, 4, 0x50000);
    map(10, 3, 0x22000);
    REQUIRE(memory.GetSpan(0x8000, 0x2001) == nullptr);
    map(9, 5, 0x21000);
    REQUIRE(memory.GetSpan(0x8000, 0x3000) == backing + 0x20000);
    table.entries.GetAndFault(9).Store(false, Common::PageType::Unmapped, 0, 0);
    REQUIRE(memory.GetSpan(0x8000, 0x3000) == nullptr);
    map(12, Common::PageTable::PageEntryData::INVALID_BLOCK_ID, 0x30000);
    map(13, 7, 0x31000);
    const Core::Memory::Memory& const_memory = memory;
    REQUIRE(const_memory.GetSpan(0xC000, 0x2000) == backing + 0x30000);
    map(255, 8, 0x60000);
    REQUIRE(memory.GetSpan(0xffff8, 8) == backing + 0x60ff8);
    REQUIRE(memory.GetSpan(0xffff8, 9) == nullptr);
    REQUIRE(memory.GetSpan(0x100000, 0) == nullptr);
    REQUIRE(memory.GetSpan(0x100000, 1) == nullptr);
    REQUIRE(memory.GetSpan((std::numeric_limits<u64>::max)() - 3, 8) == nullptr);
    REQUIRE(memory.GetSpan(0x4000, (std::numeric_limits<size_t>::max)()) == nullptr);
}
