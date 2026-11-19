// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/host_memory.h"

namespace {
constexpr size_t Page = 4096;
constexpr size_t Batch = 256 * Page;
std::vector<uintptr_t> resident;
struct Attempt { uintptr_t begin; size_t size; };
std::vector<Attempt> attempts;
std::vector<uintptr_t> accepted;
uintptr_t failing_seam{};
bool not_locked{};
}

BOOL WINAPI TgmTestQueryWorkingSet(HANDLE, PVOID information, DWORD length) {
    if (length < (resident.size() + 1) * sizeof(ULONG_PTR)) {
        SetLastError(ERROR_BAD_LENGTH);
        return FALSE;
    }
    auto* words = static_cast<ULONG_PTR*>(information);
    words[0] = resident.size();
    std::copy(resident.begin(), resident.end(), words + 1);
    return TRUE;
}

BOOL WINAPI TgmTestVirtualUnlock(LPVOID address, SIZE_T length) {
    const auto begin = reinterpret_cast<uintptr_t>(address);
    attempts.push_back({begin, length});
    if (failing_seam && begin < failing_seam && begin + length > failing_seam) {
        SetLastError(ERROR_INVALID_ADDRESS);
        return FALSE;
    }
    for (size_t offset = 0; offset < length; offset += Page) {
        accepted.push_back(begin + offset);
    }
    SetLastError(not_locked ? ERROR_NOT_LOCKED : ERROR_SUCCESS);
    return not_locked ? FALSE : TRUE;
}

TEST_CASE("Zero-page trimming caps unlock batches and preserves gaps and exact page coverage") {
    Common::HostMemory memory{4 * 1024 * 1024, 16 * 1024 * 1024};
    auto* backing = memory.BackingBasePointer();
    REQUIRE(backing != nullptr);
    std::memset(backing, 0, 4 * 1024 * 1024);
    const uintptr_t base = reinterpret_cast<uintptr_t>(backing);
    for (const unsigned scenario : {0U, 1U, 2U, 3U}) {
        CAPTURE(scenario);
        resident.clear(); attempts.clear(); accepted.clear();
        for (size_t i = 0; i < 604; ++i) {
            if (i != 300) resident.push_back(base + i * Page);
        }
        std::reverse(resident.begin(), resident.end());
        resident.push_back(resident.front());
        auto expected = resident;
        std::sort(expected.begin(), expected.end());
        expected.erase(std::unique(expected.begin(), expected.end()), expected.end());
        failing_seam = scenario == 1 ? base + 193 * Page : 0;
        not_locked = scenario == 2;
        if (scenario == 3) {
            // A resident nonzero page must stay in the working set.
            backing[10 * Page] = 1;
            expected.erase(std::find(expected.begin(), expected.end(), base + 10 * Page));
        }
        (void)memory.TrimZeroPages();
        REQUIRE_FALSE(attempts.empty());
        CHECK(std::ranges::all_of(attempts, [](const auto& attempt) {
            return attempt.size <= Batch && attempt.size % Page == 0;
        }));
        std::sort(accepted.begin(), accepted.end());
        CHECK(accepted == expected);
        CHECK(std::adjacent_find(accepted.begin(), accepted.end()) == accepted.end());
        if (scenario == 1) {
            CHECK(std::ranges::any_of(attempts, [](const auto& attempt) { return attempt.size == Page; }));
        }
        if (scenario == 2) CHECK(attempts.size() == 4);
        backing[10 * Page] = 0;
    }
}
