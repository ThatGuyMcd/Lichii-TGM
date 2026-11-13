// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cstring>
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
#include "video_core/host1x/gpu_device_memory_manager.h"

TEST_CASE("Sparse device writes survive until backing is available", "[device_kept_memory]") {
    ScopedSystemTestContext context;
    Core::System system;
    system.Initialize();
    Kernel::KProcess process{system.Kernel()};
    SCOPE_EXIT { system.Kernel().UnregisterKernelObject(&process); };
    auto& table = process.GetPageTable().GetImpl();
    table.Resize(20, 12);
    auto* page = system.DeviceMemory().buffer.BackingBasePointer() + 0x10000;
    std::memset(page, 0xcc, 0x1000);
    table.entries.GetAndFault(4).Store(false, Common::PageType::Memory, 1,
                                     reinterpret_cast<uintptr_t>(page) - 0x4000);
    auto& memory = process.GetMemory();
    memory.SetCurrentPageTable(process);
    Tegra::MaxwellDeviceMemoryManager device{system.DeviceMemory()};
    const auto asid = device.RegisterProcess(&memory);
    SCOPE_EXIT { device.UnregisterProcess(asid); };
    constexpr DAddr address = 0x20000;
    device.KeepWrites(address, 0x1000);
    std::array<u8, 32> expected{};
    expected.fill(0x6d);
    device.WriteBlockUnsafe(address + 0x7f8, expected.data(), expected.size());
    std::array<u8, 32> actual{};
    device.ReadBlockUnsafe(address + 0x7f8, actual.data(), actual.size());
    REQUIRE(actual == expected);
    REQUIRE(device.GetPointer<u8>(address) == nullptr);
    device.Map(address, 0x4000, 0x1000, asid, false);
    REQUIRE(device.GetPointer<u8>(address) == page);
    REQUIRE(std::equal(expected.begin(), expected.end(), page + 0x7f8));
    REQUIRE(page[0] == 0);
    REQUIRE(page[0xfff] == 0);

    // Model a caller that observed null before Map consumed the final kept page.
    expected.fill(0x92);
    REQUIRE(device.WriteKept(address + 0x7f8, expected.data(), expected.size()));
    REQUIRE(device.ReadKept(address + 0x7f8, actual.data(), actual.size()));
    REQUIRE(actual == expected);
    REQUIRE(std::equal(expected.begin(), expected.end(), page + 0x7f8));
}

TEST_CASE("Kept device spans zero holes and reject empty or wrapping ranges",
          "[device_kept_memory]") {
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device{physical};
    constexpr DAddr address = 0x30000;
    constexpr DAddr end = DAddr{1} << Tegra::MaxwellDeviceMemoryManager::AS_BITS;
    std::array<u8, 32> input{};
    input.fill(0x5a);
    std::array<u8, 32> output{};
    device.KeepWrites(0, 0);
    device.KeepWrites(end - 4, 8);
    device.KeepWrites((std::numeric_limits<DAddr>::max)() - 3, 16);
    REQUIRE_FALSE(device.WriteKept(end - 4, input.data(), input.size()));
    REQUIRE_FALSE(device.ReadKept(address, output.data(), 0));
    device.KeepWrites(address, 0x1000);
    REQUIRE(device.WriteKept(address + 0xff0, input.data(), input.size()));
    output.fill(0xcc);
    REQUIRE(device.ReadKept(address + 0xff0, output.data(), output.size()));
    REQUIRE(std::all_of(output.begin(), output.begin() + 16, [](u8 v) { return v == 0x5a; }));
    REQUIRE(std::all_of(output.begin() + 16, output.end(), [](u8 v) { return v == 0; }));
    device.KeepWrites(address + 0x1000, 0x1000);
    device.WriteBlockUnsafe(address + 0xff0, input.data(), input.size());
    device.ReadBlockUnsafe(address + 0xff0, output.data(), output.size());
    REQUIRE(output == input);
    REQUIRE_FALSE(device.ReadKept(end - 4, output.data(), output.size()));
    REQUIRE_FALSE(device.WriteKept(address, input.data(), 0));
}
