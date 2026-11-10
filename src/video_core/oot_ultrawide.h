// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"

// The Legend of Zelda: Ocarina of Time at 21:9 (the "Ultrawide 21:9" Add-On). The game renders a
// 21:9 frame; its camera, HUD, menus and movie layer are adapted inside the game (patch_manager.cpp).
namespace VideoCommon::OotUltrawide {

/// A frame noticeably wider than 16:9 (more than 1 %).
[[nodiscard]] constexpr bool IsWideFrame(u32 width, u32 height) {
    return height != 0 && u64{width} * 9 * 100 > u64{height} * 16 * 101;
}

/// Within 1 % of 16:9.
[[nodiscard]] constexpr bool IsSixteenNine(u32 width, u32 height) {
    const u64 a = u64{width} * 9;
    const u64 b = u64{height} * 16;
    return height != 0 && (a > b ? a - b : b - a) * 100 <= b;
}

/// The movie network writes a frame-sized image from a 16:9 movie frame; at 21:9 that image is
/// 21:9, and the game's movie layer draws it into the centred 16:9 of the frame. Whether the
/// network's stand-in may fill such an output from such an input.
[[nodiscard]] constexpr bool IsWideMovieOutput(u32 input_width, u32 input_height, u32 output_width,
                                               u32 output_height) {
    return IsSixteenNine(input_width, input_height) && IsWideFrame(output_width, output_height) &&
           output_height > input_height && output_height <= input_height * 4;
}

/// Restrict the stand-in to the selected game profile; narrow profiles letterbox vertically.
[[nodiscard]] constexpr bool IsAspectMovieOutput(u32 input_width, u32 input_height, u32 output_width,
                                                u32 output_height, u32 target_width,
                                                u32 target_height) {
    const u64 a = u64{output_width} * target_height;
    const u64 b = u64{output_height} * target_width;
    return target_width != 0 && target_height != 0 && output_width != 0 &&
           IsSixteenNine(input_width, input_height) &&
           !IsSixteenNine(output_width, output_height) &&
           output_height > input_height && u64{output_height} <= u64{input_height} * 4 &&
           (a > b ? a - b : b - a) * 100 <= b;
}

} // namespace VideoCommon::OotUltrawide
