// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <span>

#include "common/common_types.h"

namespace Tegra::OotMacros {

// Native replacements for Ocarina of Time's hot 3D macros (NVN2 Turing MME code, run on the split
// GPU thread's front by Maxwell3D::FrontRunMacro). A replacement is found by the hash of the
// macro's code (HashMacro), so it only runs for the exact code it was written against.
// Rejected calls have no effects and use the normal interpreter/JIT path. Conditional-render
// macro E2C is never replaced. Instance draws emit the original method sequence and use the
// existing draw manager's batching; these handlers do not inject folding hints.
// Context: Count/Param access the FIFO, Dram returns channel-owned data RAM, and Emit and
// EmitRepeated preserve method order through the normal front write path.

enum class Id : u8 {
    None,
    E02,
    E04,
    E08,
    E10,
    E1C,
    E1E,
    E20,
    E28,
    E2A,
    E2C,
    E36,
    E3A,
    E3E,
    E52,
    E5C,
    E68,
    E8C,
    E92,
    E96,
    E9A,
    EA0,
    ECE,
    ED6,
};

struct Macro {
    u64 hash;
    const char* name;
    Id id;
};

/// Hash of a macro's code: its instructions (three words each) from `start` through the first
/// one with the end bit and its delay slot, at most 1024 instructions. Stable across builds.
[[nodiscard]] u64 HashMacro(std::span<const u32> code, u32 start);

/// The replacement for the macro with code hash `hash`, or nullptr.
[[nodiscard]] const Macro* Find(u64 hash);

/// A macro slot can be rebound or its uploaded instructions replaced independently.
class Lookup {
public:
    Id Resolve(std::span<const u32> code, u64 generation, u32 start) {
        if (!checked || code_generation != generation || code_start != start) {
            code_generation = generation;
            code_start = start;
            const Macro* macro = Find(HashMacro(code, start));
            id = macro != nullptr ? macro->id : Id::None;
            checked = true;
        }
        return id;
    }
private:
    u64 code_generation{};
    u32 code_start{};
    Id id{};
    bool checked{};
};

inline constexpr u32 DataRamWords = 0xc00;
inline constexpr u32 FirstMacroMethod = 0xe00;
/// Instance loops longer than this run through the JIT (its instruction limit is ~1,000,000).
inline constexpr u32 MaxInstances = 0x10000;

namespace Detail {

[[nodiscard]] constexpr s32 Signed(u32 value) {
    return static_cast<s32>(value);
}

/// Whether `writes` writes at `stride` from `first` stay below the macro methods.
[[nodiscard]] constexpr bool MethodsFit(u32 first, u64 writes, u32 stride) {
    return writes == 0 || first + (writes - 1) * stride < FirstMacroMethod;
}

// 0xe02 (start 0x21): binds a constant buffer to a stage, caching it per stage and slot.
//   r5 = p0; if (p0 >= 8 signed) { r1 = 0x480 | (p0 & 3) << 5 | (p1 & 15) << 1;
//   r5 = dram[0x520 + p0]; dram[r1] = p2; dram[r1 + 1] = p3; }  if (r5 >= 15 signed) stop;
//   8e0 = (p3 >> 12) & 0xfffff, 8e1 = p3 & 0xff, 8e2 = p2; (0x904 | (r5 & 7) << 3) = 1 | (p1 + 3) << 4
template <typename C>
bool E02(C& c) {
    if (c.Count() < 4) {
        return false;
    }
    const u32 p0 = c.Param(0), p1 = c.Param(1), p2 = c.Param(2), p3 = c.Param(3);
    u32* const dram = c.Dram();
    u32 stage = p0;
    if (Signed(p0) >= 8) {
        const u32 index = 0x520 + p0;
        if (index >= DataRamWords) {
            return false;
        }
        const u32 cache = 0x480 | ((p0 & 3) << 5) | ((p1 & 15) << 1);
        stage = dram[index];
        dram[cache] = p2;
        dram[cache + 1] = p3;
    }
    if (Signed(stage) >= 15) {
        return true;
    }
    c.Emit(0x8e0, (p3 >> 12) & 0xfffff);
    c.Emit(0x8e1, p3 & 0xff);
    c.Emit(0x8e2, p2);
    c.Emit(0x904 | ((stage & 7) << 3), 1 | ((p1 + 3) << 4));
    return true;
}

// 0xe08 (start 0x8): a stage's program; caches (p1..p4) at dram[0x500 + 4 * p0].
//   b = 0x500 + 4 * p0; if (p1 == dram[b]) stop; e = dram[0x52c] & 1 << p0;
//   dram[b..b+3] = p1..p4; if (!e) stop; s = ((p4 >> 24) & 7) << 4;
//   805|s = p2 >> 24, 806|s = (p2 & 0xffffff) << 8, 807|s = (p4 >> 9) & 0x7f, 8e0 = 0x10000,
//   8e1 = p3 >> 24, 8e2 = (p3 & 0xffffff) << 8, (0x904 | ((p4 >> 28) & 7) << 3) = 0x11,
//   803|s = p4 & 0x1ff, 800|s = 1 | s
template <typename C>
bool E08(C& c) {
    if (c.Count() < 2) {
        return false;
    }
    u32* const dram = c.Dram();
    const u32 p0 = c.Param(0), p1 = c.Param(1);
    const u32 base = 0x500 + p0 * 4;
    if (base >= DataRamWords) {
        return false;
    }
    if (p1 == dram[base]) {
        return true;
    }
    if (c.Count() < 5 || base + 3 >= DataRamWords) {
        return false;
    }
    const u32 p2 = c.Param(2), p3 = c.Param(3), p4 = c.Param(4);
    const u32 enabled = dram[0x52c] & (1U << (p0 & 31));
    dram[base] = p1;
    dram[base + 1] = p2;
    dram[base + 2] = p3;
    dram[base + 3] = p4;
    if (enabled == 0) {
        return true;
    }
    const u32 stage = ((p4 >> 24) & 7) << 4;
    c.Emit(0x805 | stage, p2 >> 24);
    c.Emit(0x806 | stage, (p2 & 0xffffff) << 8);
    c.Emit(0x807 | stage, (p4 >> 9) & 0x7f);
    c.Emit(0x8e0, 0x10000);
    c.Emit(0x8e1, p3 >> 24);
    c.Emit(0x8e2, (p3 & 0xffffff) << 8);
    c.Emit(0x904 | (((p4 >> 28) & 7) << 3), 0x11);
    c.Emit(0x803 | stage, p4 & 0x1ff);
    c.Emit(0x800 | stage, 1 | stage);
    return true;
}

// 0xe28 (start 0x12e): s = dram[0x52d]; dram[0x52e] = p0; if (p0 & 1) { dram[0x52f + i] = p(1+i),
// and if (!s) 490 + i = p(1+i), for i in 0..7 }; if (!s) 82 = p0 & 1.
template <typename C>
bool E28(C& c) {
    if (c.Count() < 1) {
        return false;
    }
    const u32 p0 = c.Param(0);
    const u32 flag = p0 & 1;
    if (flag != 0 && c.Count() < 9) {
        return false;
    }
    u32* const dram = c.Dram();
    const u32 skip = dram[0x52d];
    dram[0x52e] = p0;
    if (flag != 0) {
        for (u32 i = 0; i < 8; ++i) {
            dram[0x52f + i] = c.Param(1 + i);
        }
        if (skip == 0) {
            for (u32 i = 0; i < 8; ++i) {
                c.Emit(0x490 + i, c.Param(1 + i));
            }
        }
    }
    if (skip == 0) {
        c.Emit(0x82, flag);
    }
    return true;
}

// 0xe3e (start 0x1a7): no parameters; 650 = bits of a data RAM word picked by dram[0x52d].
template <typename C>
bool E3E(C& c) {
    const u32* const dram = c.Dram();
    const u32 mode = dram[0x52d];
    u32 word;
    if (mode == 0) {
        word = dram[0x507];
        if (dram[0x508] != 0) {
            word = dram[0x50b];
        }
        if (dram[0x50c] != 0) {
            word = dram[0x50f];
        }
        if (dram[0x510] != 0) {
            word = dram[0x513];
        }
    } else {
        word = mode == 2 ? dram[0x523] : dram[0x51f];
    }
    const auto bit = [word](u32 from, u32 to) { return ((word >> from) & 1) << to; };
    u32 value = bit(16, 0) | bit(17, 4);
    if (((word >> 20) & 15) != 0) {
        value |= bit(20, 16) | bit(21, 20) | bit(22, 24) | bit(23, 28);
    }
    value |= bit(18, 8) | bit(19, 12);
    c.Emit(0x650, value);
    return true;
}

// The instance loop of 0xea0: `instances` times draw.begin, index_buffer.count and
// draw.end, the instances after the first with draw.begin's instance id Subsequent.
template <typename C>
void DrawLoop(C& c, u32 begin, u32 count, u32 instances) {
    c.Emit(0x586, begin);
    c.Emit(0x5f8, count);
    c.Emit(0x585, 0);
    if (instances > 1) {
        const u32 subsequent = (begin & ~(3U << 26)) | (1U << 26);
        const std::array<u32, 2> rest[3]{{0x586, subsequent}, {0x5f8, count}, {0x585, 0}};
        c.EmitRepeated(rest, 3, instances - 1);
    }
}

// 0xe68 (start 0x262): dram[p0] = p1.
template <typename C>
bool E68(C& c) {
    if (c.Count() < 2 || c.Param(0) >= DataRamWords) {
        return false;
    }
    c.Dram()[c.Param(0)] = c.Param(1);
    return true;
}

// 0xe8c (start 0x2f7): takes one parameter, does nothing.
template <typename C>
bool E8C(C& c) {
    return c.Count() >= 1;
}

// 0xe92 (start 0x302): o = dram[0x54b]; dram[0x54b] = p0; if (o | p0) 47d = 0.
template <typename C>
bool E92(C& c) {
    if (c.Count() < 1) {
        return false;
    }
    u32* const dram = c.Dram();
    const u32 p0 = c.Param(0);
    const u32 any = dram[0x54b] | p0;
    dram[0x54b] = p0;
    if (any != 0) {
        c.Emit(0x47d, 0);
    }
    return true;
}

// 0x3a400040.
template <typename C>
bool E96(C& c) {
    if (c.Count() < 1) {
        return false;
    }
    u32* const dram = c.Dram();
    const u32 pairs = c.Param(0);
    const u32 pad = dram[0x54c] - pairs;
    const u64 pad_pairs = Signed(pad) > 0 ? pad : 0;
    if (u64{pairs} * 2 + 1 > c.Count() || !MethodsFit(0x458, (u64{pairs} + pad_pairs) * 2, 1)) {
        return false;
    }
    u32 method = 0x458;
    for (u32 i = 0; i < pairs * 2; ++i) {
        c.Emit(method++, c.Param(1 + i));
    }
    dram[0x54c] = pairs;
    for (u64 i = 0; i < pad_pairs * 2; ++i) {
        c.Emit(method++, 0x3a400040);
    }
    return true;
}

// 0xe9a (start 0x311): vertex streams. n0 = p0 pairs of parameters to 700 + 4i, zero for the
// pairs beyond n0 the last call set (dram[0x54d]); then n1 = p(1+2n0), b = p(2+2n0): n1 pairs of
// parameters to 703 + 4i, then 620 + j = bit j of b for j < 2n1, zero for the pairs beyond n1 the
// last call set (dram[0x54e]).
template <typename C>
bool E9A(C& c) {
    const u32 count = c.Count();
    if (count < 1) {
        return false;
    }
    u32* const dram = c.Dram();
    const u32 n0 = c.Param(0);
    if (u64{n0} * 2 + 3 > count) {
        return false;
    }
    const u32 n1 = c.Param(1 + n0 * 2);
    const u32 bits = c.Param(2 + n0 * 2);
    if (u64{n0} * 2 + 3 + u64{n1} * 2 > count) {
        return false;
    }
    const u32 pad0 = dram[0x54d] - n0;
    const u64 pad0_pairs = Signed(pad0) > 0 ? pad0 : 0;
    const u32 pad1 = dram[0x54e] - n1;
    const u64 pad1_pairs = Signed(pad1) > 0 ? pad1 : 0;
    if (!MethodsFit(0x700, (u64{n0} + pad0_pairs) * 2, 4) || !MethodsFit(0x703, u64{n1} * 2, 4) ||
        !MethodsFit(0x620, (u64{n1} + pad1_pairs) * 2, 1)) {
        return false;
    }
    u32 method = 0x700;
    for (u32 i = 0; i < n0 * 2; ++i, method += 4) {
        c.Emit(method, c.Param(1 + i));
    }
    dram[0x54d] = n0;
    for (u64 i = 0; i < pad0_pairs * 2; ++i, method += 4) {
        c.Emit(method, 0);
    }
    const u32 base = 3 + n0 * 2;
    method = 0x703;
    for (u32 i = 0; i < n1 * 2; ++i, method += 4) {
        c.Emit(method, c.Param(base + i));
    }
    method = 0x620;
    u32 rest = bits;
    for (u32 i = 0; i < n1; ++i, rest >>= 2) {
        c.Emit(method++, rest & 1);
        c.Emit(method++, (rest >> 1) & 1);
    }
    dram[0x54e] = n1;
    for (u64 i = 0; i < pad1_pairs * 2; ++i) {
        c.Emit(method++, 0);
    }
    return true;
}

// 0xea0 (start 0x1e6): instanced indexed draw with the base vertex and instance in the driver's
// constant buffer (offset 0x640). Parameters as 0xe52.
//   5f7 = p3, 446 = p4, 50d = p4, 50e = p5, 8e3 = 0x640, 8e4 = p4, 8e5 = p5, DrawLoop(p0, p1, p2),
//   446 = 0, 50d = 0, 50e = 0, 8e3 = 0x640, 8e4 = 0, 8e5 = 0
template <typename C>
bool EA0(C& c) {
    if (c.Count() < 6) {
        return false;
    }
    const u32 instances = c.Param(2);
    if (instances > MaxInstances) {
        return false;
    }
    const u32 base_vertex = c.Param(4), base_instance = c.Param(5);
    c.Emit(0x5f7, c.Param(3));
    c.Emit(0x446, base_vertex);
    c.Emit(0x50d, base_vertex);
    c.Emit(0x50e, base_instance);
    c.Emit(0x8e3, 0x640);
    c.Emit(0x8e4, base_vertex);
    c.Emit(0x8e5, base_instance);
    if (instances != 0) {
        DrawLoop(c, c.Param(0), c.Param(1), instances);
    }
    c.Emit(0x446, 0);
    c.Emit(0x50d, 0);
    c.Emit(0x50e, 0);
    c.Emit(0x8e3, 0x640);
    c.Emit(0x8e4, 0);
    c.Emit(0x8e5, 0);
    return true;
}

// 0xed6 (start 0x4ee): s = dram[0x545] with the bits p0 >> 16 selects taken from p0;
// dram[0x545] = s; 47c = a bit of s picked by dram[0x52d] and bits 5 and 7 of s.
template <typename C>
bool ED6(C& c) {
    if (c.Count() < 1) {
        return false;
    }
    u32* const dram = c.Dram();
    const u32 p0 = c.Param(0);
    const u32 mask = p0 >> 16;
    const u32 state = (dram[0x545] & ~mask) | (p0 & mask);
    u32 value;
    if (dram[0x52d] == 0) {
        value = state & 1;
        if (((state >> 5) & 1) != 0) {
            value = (state >> 4) & 1;
        }
        if (((state >> 7) & 1) != 0) {
            value = (state >> 6) & 1;
        }
    } else {
        value = (state >> 8) & 1;
    }
    dram[0x545] = state;
    c.Emit(0x47c, value);
    return true;
}

} // namespace Detail

/// Runs replacement `id` (Find) for the call in `c`. False: run the call through the JIT.
template <typename C>
bool Run(Id id, C& c) {
    switch (id) {
    case Id::E02:
        return Detail::E02(c);
    case Id::E04:
        return false;
    case Id::E08:
        return Detail::E08(c);
    case Id::E10:
        return false;
    case Id::E1C:
        return false;
    case Id::E1E:
        return false;
    case Id::E20:
        return false;
    case Id::E28:
        return Detail::E28(c);
    case Id::E2A:
        return false;
    case Id::E2C:
        return false;
    case Id::E36:
        return false;
    case Id::E3A:
        return false;
    case Id::E3E:
        return Detail::E3E(c);
    case Id::E52:
        return false;
    case Id::E5C:
        return false;
    case Id::E68:
        return Detail::E68(c);
    case Id::E8C:
        return Detail::E8C(c);
    case Id::E92:
        return Detail::E92(c);
    case Id::E96:
        return false;
    case Id::E9A:
        return Detail::E9A(c);
    case Id::EA0:
        return Detail::EA0(c);
    case Id::ECE:
        return false;
    case Id::ED6:
        return Detail::ED6(c);
    case Id::None:
        break;
    }
    return false;
}

/// Emits through the caller's normal register/shadow/cache path. No side effects occur when
/// a handler's preconditions fail, so the caller can immediately run the original macro.
template <typename Emitter>
bool Run(Id id, std::span<const u32> parameters, std::span<u32> ram, const Emitter& emit) {
    if (ram.size() != DataRamWords) {
        return false;
    }
    struct Context {
        std::span<const u32> parameters;
        std::span<u32> ram;
        const Emitter& emit;
        u32 Count() const { return static_cast<u32>(parameters.size()); }
        u32 Param(u32 index) const { return parameters[index]; }
        u32* Dram() { return ram.data(); }
        void Emit(u32 method, u32 value) { emit(method, value); }
        void EmitRepeated(const std::array<u32, 2>* writes, u32 count, u32 times) {
            for (u32 time = 0; time < times; ++time) {
                for (u32 i = 0; i < count; ++i) {
                    emit(writes[i][0], writes[i][1]);
                }
            }
        }
    } context{parameters, ram, emit};
    return Run(id, context);
}

} // namespace Tegra::OotMacros
