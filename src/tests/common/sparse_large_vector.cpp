// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "common/page_table.h"
#include "common/sparse_large_vector.h"
#include "common/scope_exit.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using Entry = Common::PageTable::PageEntryData;

const std::size_t EntriesPerHostPage = Common::HostPageSize / sizeof(Entry);

} // namespace

#ifdef _WIN32
TEST_CASE("SparseLargeVector read faults leave the page writable", "[common][loader_bounds]") {
    auto* region = Common::AllocateMemoryPages(Common::HostPageSize * 2);
    REQUIRE(region != nullptr);
    SCOPE_EXIT { Common::FreeMemoryPages(region, Common::HostPageSize * 2); };
    auto* word = reinterpret_cast<volatile u64*>(region);
    // Exercise the real registered fault handler, then a second commit of the same page.
    CHECK(*word == 0);
    MEMORY_BASIC_INFORMATION info{};
    REQUIRE(VirtualQuery(region, &info, sizeof(info)) == sizeof(info));
    CHECK(info.State == MEM_COMMIT);
    REQUIRE(info.Protect == PAGE_READWRITE);
    *word = 0x123456789ABCDEF0ULL;
    REQUIRE(Common::CommitVectorPage(reinterpret_cast<uintptr_t>(region)));
    REQUIRE(VirtualQuery(region, &info, sizeof(info)) == sizeof(info));
    REQUIRE(info.Protect == PAGE_READWRITE);
    CHECK(*word == 0x123456789ABCDEF0ULL);
    *word = 0x42;
    CHECK(*word == 0x42);
}
#endif

TEST_CASE("SparseLargeVector zeroes every entry of a region", "[common]") {
    Common::SparseLargeVector<Entry> entries(EntriesPerHostPage * 4);
    const std::size_t begin = EntriesPerHostPage / 2;
    const std::size_t end = EntriesPerHostPage * 3 + EntriesPerHostPage / 2;

    entries.CommitRegion(0, entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        entries.GetUnchecked(i).Store(false, Common::PageType::Memory, 1, 0x1000);
    }

    entries.ZeroRegion(begin, end);

    for (std::size_t i = 0; i < entries.size(); ++i) {
        const bool zeroed = i >= begin && i < end;
        REQUIRE((entries[i].Type() == Common::PageType::Unmapped) == zeroed);
    }
}

// A thread that loaded an entry before an unmap may still store to it afterwards, as
// RasterizerMarkRegionCached does when the GPU uncaches memory the guest is unmapping.
// That store must land in memory the vector still tracks, so the page stays writable and
// the next unmap clears it.
TEST_CASE("SparseLargeVector keeps zeroed pages writable for in-flight stores", "[common]") {
    Common::SparseLargeVector<Entry> entries(EntriesPerHostPage * 2);
    const std::size_t index = EntriesPerHostPage + 3;

    entries.CommitRegion(index, index + 1);
    Entry& entry = entries.GetUnchecked(index);
    entry.Store(false, Common::PageType::RasterizerCachedMemory, 1, 0x1000);

    entries.ZeroRegion(EntriesPerHostPage, EntriesPerHostPage * 2);
    REQUIRE(entries[index].Type() == Common::PageType::Unmapped);

    entry.Store(false, Common::PageType::Memory, 1, 0x1000);
    REQUIRE(entries[index].Type() == Common::PageType::Memory);

    entries.ZeroRegion(EntriesPerHostPage, EntriesPerHostPage * 2);
    REQUIRE(entries[index].Type() == Common::PageType::Unmapped);
}
