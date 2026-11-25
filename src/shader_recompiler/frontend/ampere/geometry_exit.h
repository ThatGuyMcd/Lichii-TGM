// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include "common/common_types.h"
#include "shader_recompiler/frontend/ampere/instruction.h"

namespace Shader::Ampere {

struct GeometryExitEnvelope {
    struct Exit {
        u32 offset;
        u32 predicate;
    };
    std::vector<Exit> exits;
    u32 handler{};
    bool has_returns{};
    u32 error_offset{};
    std::string_view error;

    explicit operator bool() const { return error.empty(); }
};

inline constexpr u32 GeometryExitSetupInstructions = 5;

namespace GeometryExitEncoding {

inline constexpr u32 Always = 7;
inline constexpr u32 Never = Always | 8;
inline constexpr u32 ZeroRegister = 255;
inline constexpr u32 ModeMask = 0xc00001ffU;
inline constexpr u32 DefaultExitPredicate = Always << 23;

inline bool Matches(const Instruction& instruction, u32 opcode, u32 dest = 0, u32 src = 0,
                    u32 operand = 0, u32 modifiers = 0) {
    return instruction.Bits(0, 12) == opcode && instruction.Bits(12, 4) == Always &&
           instruction.Bits(16, 8) == dest && instruction.Bits(24, 8) == src &&
           instruction.words[1] == operand && instruction.words[2] == modifiers &&
           (instruction.words[3] & ModeMask) == 0;
}

} // namespace GeometryExitEncoding

// Accept only the register allocation the lowerer can discard after its liveness check.
inline std::optional<u32> DecodeGeometryExitTarget(
    std::span<const Instruction, GeometryExitSetupInstructions> setup) {
    using namespace GeometryExitEncoding;
    constexpr u32 PcLow = 0, PcHigh = 1, OffsetRegister = 3;
    constexpr u32 CarryPredicate = 0, CarryIn = 1U << 10;
    constexpr u32 DiscardCarry = Always << 17, DiscardSecondCarry = Always << 20;
    constexpr u32 NoSecondCarryIn = Never << 13;
    constexpr u32 LowAdd = ZeroRegister | NoSecondCarryIn | (CarryPredicate << 17) |
                           DiscardSecondCarry | (Never << 23);
    constexpr u32 HighAdd = ZeroRegister | CarryIn | NoSecondCarryIn |
                            DiscardCarry | DiscardSecondCarry | (CarryPredicate << 23);
    constexpr u32 HighMultiplyAdd = PcHigh | CarryIn | DiscardCarry | (CarryPredicate << 23);
    const u32 target = setup[1].words[1];
    if (!Matches(setup[0], 0x34e, PcLow) ||
        !Matches(setup[1], 0x802, OffsetRegister, 0, target, 15U << 8) ||
        !Matches(setup[2], 0x210, PcLow, PcLow, OffsetRegister, LowAdd) ||
        !(Matches(setup[3], 0x210, PcHigh, PcHigh, ZeroRegister, HighAdd) ||
          Matches(setup[3], 0x224, PcHigh, ZeroRegister, ZeroRegister, HighMultiplyAdd)) ||
        !Matches(setup[4], 0x357, 0, PcLow)) {
        return std::nullopt;
    }
    return target;
}

template <typename Code>
GeometryExitEnvelope AnalyzeGeometryExit(const Code& code) {
    using namespace GeometryExitEncoding;
    constexpr size_t Setup = GeometryExitSetupInstructions;
    GeometryExitEnvelope result;
    const auto fail = [&](size_t index, std::string_view reason) {
        result.exits.clear();
        result.handler = 0;
        result.has_returns = false;
        result.error_offset = static_cast<u32>(index * 16);
        result.error = reason;
        return result;
    };
    const auto bits = [&](size_t index, u32 offset, u32 count) {
        const auto& words = code[index].words;
        u64 value = words[offset / 32];
        if (offset / 32 + 1 < 4) value |= u64{words[offset / 32 + 1]} << 32;
        return static_cast<u32>((value >> (offset % 32)) & ((u64{1} << count) - 1));
    };
    const auto is_exit = [&](size_t index) {
        const auto& words = code[index].words;
        return (words[0] & ~0xf000U) == 0x94d && words[1] == 0 &&
               words[2] == DefaultExitPredicate && (words[3] & ModeMask) == 0 &&
               bits(index, 12, 4) != Never;
    };
    const auto is_return = [&](size_t index) {
        const auto& words = code[index].words;
        const u64 raw = words[1] | (u64{words[2] & 0x3ffff} << 32);
        const s64 relative = static_cast<s64>(raw << 14) >> 14;
        constexpr u32 NoStackAdjustment = 1U << 22;
        return bits(index, 0, 12) == 0x950 && bits(index, 12, 4) == Always &&
               bits(index, 16, 8) == 0 && bits(index, 24, 8) < 249 &&
               (words[2] & ~0x3ffffU) == (DefaultExitPredicate | NoStackAdjustment) &&
               (words[3] & ModeMask) == 0 &&
               static_cast<s64>(index + 1) * 16 + relative == 0;
    };

    const size_t count = code.size();
    if (count < Setup + 4) return fail(count, "native exit envelope truncated");
    const auto decoded_target =
        DecodeGeometryExitTarget(std::span<const Instruction, Setup>{code.data(), Setup});
    if (!decoded_target)
        return fail(0, "native exit handler installation");
    const u32 target = *decoded_target;
    const size_t handler = target / 16;
    if (target % 16 || handler <= Setup + 1 || handler + 3 != count)
        return fail(1, "native exit handler location");
    if (!Matches(code[handler], 0x324, ZeroRegister, 0, ZeroRegister) ||
        !Matches(code[handler + 1], 0x957) ||
        !Matches(code[handler + 2], 0x94d, 0, 0, 0, DefaultExitPredicate))
        return fail(handler, "native exit handler body");
    if (!(is_exit(handler - 1) && bits(handler - 1, 12, 4) == 7) && !is_return(handler - 1))
        return fail(handler - 1, "native exit handler reached by fallthrough");

    for (size_t index = Setup; index < handler; ++index) {
        const u32 opcode = bits(index, 0, 12), family = opcode & 0x1ff;
        if (family == 0x14e || (family >= 0x154 && family <= 0x157))
            return fail(index, "native exit handler state access in body");
        if (opcode == 0x94d) {
            if (!is_exit(index)) return fail(index, "native body EXIT mode");
            result.exits.push_back({static_cast<u32>(index * 16), bits(index, 12, 4)});
            continue;
        }
        if (is_return(index)) {
            result.has_returns = true;
            continue;
        }
        if (family < 0x140 || family >= 0x160 || opcode == 0x941 || opcode == 0x942 ||
            opcode == 0x948)
            continue;
        const auto& words = code[index].words;
        const bool noinc_call = opcode == 0x944 && (words[2] & 0x400000) != 0;
        if (opcode != 0x945 && opcode != 0x947 && !noinc_call)
            return fail(index, "native control flow in exit envelope body");
        const u64 raw = words[1] | (u64{words[2] & 0x3ffff} << 32);
        const s64 relative = opcode == 0x945 ? s64{static_cast<s32>(words[1])}
                                             : static_cast<s64>(raw << 14) >> 14;
        const s64 destination = static_cast<s64>(index) * 16 + 16 + relative;
        if (destination % 16 || destination < static_cast<s64>(Setup * 16) ||
            destination >= static_cast<s64>(handler * 16))
            return fail(index, "native branch leaves exit envelope body");
    }

    u32 live_registers = 0b1011, live_predicates = 0b1;
    for (size_t index = Setup; live_registers || live_predicates; ++index) {
        const u32 opcode = bits(index, 0, 12), family = opcode & 0x1ff;
        const u32 form = (opcode >> 9) & 7, guard = bits(index, 12, 4);
        u32 reads = 0, read_predicates = 0, writes = 0, write_predicates = 0;
        const auto read_register = [&](u32 reg) { if (reg < 32) reads |= 1U << reg; };
        const auto write_register = [&](u32 reg) { if (reg < 32) writes |= 1U << reg; };
        const auto read_predicate = [&](u32 pred) { if (pred < 7) read_predicates |= 1U << pred; };
        const auto write_predicate = [&](u32 pred) { if (pred < 7) write_predicates |= 1U << pred; };
        const auto read_sources = [&](bool has_c) {
            read_register(bits(index, 24, 8));
            if (form == 1) read_register(bits(index, 32, 8));
            if (form == 2 || form == 3 || form == 7) read_register(bits(index, 64, 8));
            if (has_c && (form == 1 || form == 4 || form == 5 || form == 6)) read_register(bits(index, 64, 8));
        };
        read_predicate(guard & 7);
        if (is_exit(index)) {
            if (guard != 7)
                return fail(index, "native instruction before exit setup state is dead");
            // The handler finalizes the output through R0; the rest of the setup state may stay
            // live when the body ends without reading it.
            if ((live_registers & 1) || (read_predicates & live_predicates))
                return fail(index, "native exit handler reads exit setup state");
            break;
        }
        if (opcode == 0x919 || opcode == 0x923) {
            if (opcode == 0x923) read_register(bits(index, 24, 8));
            write_register(bits(index, 16, 8));
        } else if (opcode == 0x805 || opcode == 0xab9 || opcode == 0x945) {
        } else if (opcode == 0x381 || opcode == 0x981) {
            const u32 address = bits(index, 24, 8), size = bits(index, 73, 3);
            if (opcode == 0x381) read_predicate(7 - bits(index, 64, 3));
            if (address != 255) {
                read_register(address);
                read_register(address + 1);
            }
            const u32 results = size < 5 ? 1 : size == 5 ? 2 : size == 6 ? 4 : 0;
            for (u32 i = 0; i < results; ++i) write_register(bits(index, 16, 8) + i);
        } else if (family == 0x002 && (form == 1 || form == 4 || form == 5 || form == 6)) {
            if (form == 1) read_register(bits(index, 32, 8));
            write_register(bits(index, 16, 8));
        } else if ((family == 0x024 || family == 0x016) && form != 0) {
            read_sources(true);
            if (family == 0x024 && bits(index, 74, 1)) read_predicate(bits(index, 87, 3));
            write_register(bits(index, 16, 8));
        } else if (family == 0x105 && (form == 1 || form == 5 || form == 6)) {
            // F2I: register, constant buffer or uniform source in operand b; a is unused.
            if (form == 1) read_register(bits(index, 32, 8));
            write_register(bits(index, 16, 8));
        } else if (family == 0x011 && form != 0) {
            // LEA (.X reads the carry predicate), optional carry out.
            read_sources(true);
            if (bits(index, 74, 1)) read_predicate(bits(index, 87, 3));
            write_register(bits(index, 16, 8));
            write_predicate(bits(index, 81, 3));
        } else if (opcode == 0xb82) {
            // LDC with a register index; a 64-bit load writes a pair.
            if (bits(index, 24, 8) != 255) read_register(bits(index, 24, 8));
            write_register(bits(index, 16, 8));
            if (bits(index, 73, 3) == 5) write_register(bits(index, 16, 8) + 1);
        } else if (family == 0x012 && form != 0) {
            // LOP3.LUT with an optional predicate result.
            read_sources(true);
            write_register(bits(index, 16, 8));
            write_predicate(bits(index, 81, 3));
        } else if (opcode == 0x321) {
            // ALD: loads 1-4 components into Rd.. from the vertex in b, indexed by a unless RZ.
            read_register(bits(index, 24, 8));
            read_register(bits(index, 32, 8));
            for (u32 i = 0; i <= bits(index, 74, 2); ++i) write_register(bits(index, 16, 8) + i);
        } else if (opcode == 0x324 || opcode == 0x924) {
            // OUT.EMIT/CUT: reads the output handle in a (and a register stream in b), writes the
            // next handle to Rd.
            read_sources(false);
            write_register(bits(index, 16, 8));
        } else if (opcode == 0x322) {
            // AST: stores 1-4 components from operand b for the vertex in c; writes nothing.
            const u32 value = bits(index, 32, 8), components = bits(index, 74, 2) + 1;
            if (value != 255)
                for (u32 i = 0; i < components; ++i) read_register(value + i);
            read_register(bits(index, 64, 8));
        } else if (family == 0x00c && form != 0 && !bits(index, 72, 1)) {
            read_sources(false);
            read_predicate(bits(index, 87, 3));
            write_predicate(bits(index, 81, 3));
            write_predicate(bits(index, 84, 3));
        } else {
            return fail(index, "native instruction before exit setup state is dead");
        }
        if ((reads & live_registers) || (read_predicates & live_predicates))
            return fail(index, "native body reads exit setup state");
        if (guard == 7) {
            live_registers &= ~writes;
            live_predicates &= ~write_predicates;
        }
    }
    result.handler = target;
    return result;
}

} // namespace Shader::Ampere
