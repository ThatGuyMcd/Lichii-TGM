// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

BOOL WINAPI TgmTestGlobalMemoryStatusEx(LPMEMORYSTATUSEX status);
BOOL WINAPI TgmTestGetPerformanceInfo(PPERFORMANCE_INFORMATION info, DWORD size);
#ifdef GetPerformanceInfo
#undef GetPerformanceInfo
#endif
#define GetPerformanceInfo TgmTestGetPerformanceInfo
#define GlobalMemoryStatusEx TgmTestGlobalMemoryStatusEx
