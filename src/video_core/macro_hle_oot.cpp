// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include "video_core/macro_hle_oot.h"

namespace Tegra::OotMacros {

namespace {

constexpr u32 MaxInstructions = 1024;

// Only handlers checked against the production interpreter using the corresponding SDK
// instruction sequences are selected. Other uploaded code keeps the normal JIT path.
constexpr std::array macros{
    Macro{0x2ee8ad56465747bfULL, "0xe02 bind constant buffer", Id::E02},
    Macro{0x999c63d624da37a4ULL, "0xe08 stage program", Id::E08},
    Macro{0xa5fc8a3a55ec5a41ULL, "0xe28", Id::E28},
    Macro{0x053e08f952ce26eeULL, "0xe3e", Id::E3E},
    Macro{0xdaba5f7044390cd4ULL, "0xe68 data RAM write", Id::E68},
    Macro{0x0fc20101c0d79a0aULL, "0xe8c", Id::E8C},
    Macro{0x57f1ff45827c7b87ULL, "0xe92", Id::E92},
    Macro{0x0d3ef03cb86ff29bULL, "0xe9a vertex streams", Id::E9A},
    Macro{0x38b7414725298d00ULL, "0xea0 instanced draw (base in constant buffer)", Id::EA0},
    Macro{0x2e38eccae827f4f1ULL, "0xed6", Id::ED6},
};

} // namespace

u64 HashMacro(std::span<const u32> code, u32 start) {
    // FNV-1a over the words, plus the instruction count.
    u64 hash = 0xcbf29ce484222325ULL;
    const auto mix = [&hash](u32 word) {
        for (u32 byte = 0; byte < 4; ++byte) {
            hash ^= (word >> (byte * 8)) & 0xff;
            hash *= 0x100000001b3ULL;
        }
    };
    u32 count = 0;
    bool end_next = false;
    for (u32 pc = start; count < MaxInstructions && size_t{pc} * 3 + 2 < code.size(); ++pc) {
        mix(code[pc * 3]);
        mix(code[pc * 3 + 1]);
        mix(code[pc * 3 + 2]);
        ++count;
        if (end_next) {
            mix(count);
            return hash;
        }
        // Bit 0 of the instruction's high word: end after the next instruction (delay slot).
        end_next = (code[pc * 3 + 2] & 1) != 0;
    }
    return 0; // Truncated, unterminated, or over-budget macro code.
}

const Macro* Find(u64 hash) {
    for (const Macro& macro : macros) {
        if (macro.hash == hash) {
            return &macro;
        }
    }
    return nullptr;
}


} // namespace Tegra::OotMacros
