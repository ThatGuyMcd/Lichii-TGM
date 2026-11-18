// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/logging.h"
#include "common/page_table.h"
#include "common/scope_exit.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/memory.h"
#include "tests/core/system_test_context.h"

namespace {

std::vector<std::string> WriteLines(unsigned bits) {
    const auto prefix = fmt::format("Unmapped Write{} @", bits);
    std::vector<std::string> result;
    for (const auto& line : Common::Log::ReadRing(0)) {
        if (line.text.find(prefix) != std::string::npos) {
            result.push_back(line.text);
        }
    }
    return result;
}

} // namespace

// The fault counters intentionally live for the host process, as the existing read counters do.
// Run this hidden case in its own CTest process: no production reset hook or order dependence.
TEST_CASE("Unmapped writes keep first and periodic diagnostics independently of mapped writes",
          "[.][core][unmapped_write_logging]") {
    ScopedSystemTestContext context;
    const auto system = std::make_unique<Core::System>();
    system->Initialize();
    Kernel::KProcess process{system->Kernel()};
    SCOPE_EXIT { system->Kernel().UnregisterKernelObject(&process); };
    auto& table = process.GetPageTable().GetImpl();
    table.Resize(20, 12);
    alignas(4096) std::array<u8, 4096> backing{};
    constexpr VAddr mapped = 0x4000;
    // Outside the 20-bit address space, within the CPU's 48-bit address mask. No sparse page
    // allocation occurs here, including in the concurrent fault test below.
    constexpr VAddr unmapped = 0x200000;
    table.entries.GetAndFault(mapped >> 12)
        .Store(false, Common::PageType::Memory, 1,
               reinterpret_cast<uintptr_t>(backing.data()) - mapped);
    auto& memory = process.GetMemory();
    memory.SetCurrentPageTable(process);

    Common::Log::Initialize();
    Common::Log::SetRingCapacity(0);
    Common::Log::SetRingCapacity(4096);
    SCOPE_EXIT { Common::Log::SetRingCapacity(0); };
    LOG_ERROR(HW_Memory, "Unmapped-write regression diagnostic control");
    const auto control = Common::Log::ReadRing(0);
    REQUIRE(std::ranges::any_of(control, [](const auto& line) {
        return line.text.find("Unmapped-write regression diagnostic control") != std::string::npos;
    }));

    const auto exercise = [&](unsigned bits, auto write, auto read) {
        CAPTURE(bits);
        Common::Log::SetRingCapacity(0);
        Common::Log::SetRingCapacity(4096);
        // A hot mapped loop must neither log nor spend the fault sampling budget.
        for (u32 i = 0; i < 100001; ++i) {
            write(mapped, 0x5a);
        }
        CHECK(read(mapped) == 0x5a);
        CHECK(backing[0] == 0x5a);
        REQUIRE(WriteLines(bits).empty());

        for (u32 i = 0; i < 1000; ++i) {
            write(unmapped, 0x31);
        }
        const auto initial = WriteLines(bits);
        REQUIRE(initial.size() == 1000);
        CHECK(initial.front().find("0x00000000200000 = 0x00000000000031") != std::string::npos);
        for (u32 i = 1000; i < 100000; ++i) {
            write(unmapped, 0x32);
        }
        REQUIRE(WriteLines(bits).size() == 1000);
        write(unmapped, 0x42);
        const auto sampled = WriteLines(bits);
        REQUIRE(sampled.size() == 1001);
        CHECK(sampled.back().find("0x00000000200000 = 0x00000000000042") != std::string::npos);
        CHECK(read(mapped) == 0x5a);
    };
    exercise(8, [&](VAddr a, u8 v) { memory.Write8(a, v); },
             [&](VAddr a) { return memory.Read8(a); });
    exercise(16, [&](VAddr a, u16 v) { memory.Write16(a, v); },
             [&](VAddr a) { return memory.Read16(a); });
    exercise(32, [&](VAddr a, u32 v) { memory.Write32(a, v); },
             [&](VAddr a) { return memory.Read32(a); });

    // Four host threads share the 64-bit counter, rather than each keeping its own first 1000.
    // The prior widths must not consume any of this width's diagnostics.
    Common::Log::SetRingCapacity(0);
    Common::Log::SetRingCapacity(4096);
    memory.Write64(mapped, 0x123456789abcdef0ULL);
    {
        std::array<std::jthread, 4> writers;
        for (auto& writer : writers) {
            writer = std::jthread([&] {
                for (u32 i = 0; i < 50000; ++i) {
                    memory.Write64(unmapped, 0x1122334455667788ULL);
                }
            });
        }
    }
    REQUIRE(WriteLines(64).size() == 1001); // Counts 0..999 and 100000.
    memory.Write64(unmapped, 0x8877665544332211ULL);
    const auto sampled = WriteLines(64);
    REQUIRE(sampled.size() == 1002); // Count 200000.
    CHECK(sampled.back().find("0x8877665544332211") != std::string::npos);
    CHECK(memory.Read64(mapped) == 0x123456789abcdef0ULL);
}
