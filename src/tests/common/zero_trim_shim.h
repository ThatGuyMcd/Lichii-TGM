// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

BOOL WINAPI TgmTestQueryWorkingSet(HANDLE process, PVOID information, DWORD length);
BOOL WINAPI TgmTestVirtualUnlock(LPVOID address, SIZE_T length);
#define K32QueryWorkingSet TgmTestQueryWorkingSet
#define VirtualUnlock TgmTestVirtualUnlock
