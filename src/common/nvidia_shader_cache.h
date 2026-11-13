// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace Common {
/// Set Linux NVIDIA cache defaults before loading the driver, preserving user overrides.
void SetUpNvidiaShaderDiskCache();
}
