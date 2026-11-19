// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include "common/memory_detect.h"

namespace {
bool status_succeeds = true;
DWORDLONG available{};
unsigned status_calls{}, performance_calls{};
DWORD requested_size{};
}

BOOL WINAPI TgmTestGlobalMemoryStatusEx(LPMEMORYSTATUSEX status) {
    ++status_calls;
    requested_size = status->dwLength;
    *status = {};
    status->dwLength = sizeof(*status);
    status->ullTotalPhys = DWORDLONG{32} << 30;
    status->ullTotalPageFile = DWORDLONG{64} << 30;
    status->ullAvailPageFile = available;
    return status_succeeds ? TRUE : FALSE;
}

BOOL WINAPI TgmTestGetPerformanceInfo(PPERFORMANCE_INFORMATION info, DWORD size) {
    ++performance_calls;
    *info = {};
    info->cb = size;
    info->PageSize = 4096;
    info->CommitTotal = 25;
    info->CommitLimit = 100;
    return TRUE;
}

TEST_CASE("Host commit uses the process-available byte value including zero and64bit values") {
    for (const DWORDLONG value : {DWORDLONG{0}, DWORDLONG{7890123}, (DWORDLONG{1} << 40) + 123}) {
        available = value;
        status_succeeds = true;
        status_calls = performance_calls = 0;
        const auto result = Common::GetSystemCommitRoom();
        CHECK(result == value);
        CHECK(status_calls == 1);
        CHECK(performance_calls == 0);
        CHECK(requested_size == sizeof(MEMORYSTATUSEX));
    }
}

TEST_CASE("An unavailable process commit query remains unknown rather than inventing room") {
    status_succeeds = false;
    status_calls = performance_calls = 0;
    CHECK_FALSE(Common::GetSystemCommitRoom().has_value());
    CHECK(status_calls == 1);
    CHECK(performance_calls == 0);
}
