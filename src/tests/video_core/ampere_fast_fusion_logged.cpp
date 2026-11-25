// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
constexpr Instruction FixtureInstruction(u32 opcode, u32 dest, u32 source_a, u32 source_b,
                                         u32 modifiers, u32 guard = 7) {
    return {{opcode | (guard << 12) | (dest << 16) | (source_a << 24),
             source_b, modifiers, 0}};
}

constexpr u32 HalfSource = 6;
constexpr u32 HalfDestination = 18;
constexpr u32 FloorSource = 23;
constexpr u32 FloorDestination = 31;
constexpr u32 IntegerSource = 14;
constexpr u32 IntegerHalfDestination = 47;
constexpr u32 FloatSource = 27;
constexpr u32 FloatDestination = 36;
constexpr u32 PackDestination = 13;
constexpr u32 PackUniform = 15;
constexpr u32 UniformMirror = 216;
constexpr u32 ScopedAddress = 20;
constexpr u32 ScopedPolicyUniform = 18;

// Independently selected operands. These format bits select the respective half,
// signed word and rounding modes without embedding any shader instruction.
constexpr u32 SignedHalfToWord = (1U << 20) | (3U << 12) | (1U << 8);
constexpr u32 HalfRoundToNearest = (1U << 20) | (1U << 11);
constexpr u32 HalfRoundTowardZero = HalfRoundToNearest | (3U << 14);
constexpr u32 ScopedOrdering = (1U << 15) | (1U << 13);
constexpr u32 WeakDescriptorLoad = (3U << 26) | (15U << 17) | (1U << 12) |
                                   (6U << 9) | (1U << 8);
constexpr auto HalfConversion = FixtureInstruction(0x305, HalfDestination, 0, HalfSource,
                                                    SignedHalfToWord);
constexpr auto HalfFloor = FixtureInstruction(0x305, FloorDestination, 0, FloorSource,
                                               SignedHalfToWord | (1U << 14));
constexpr auto SignedWordToHalf = FixtureInstruction(0x306, IntegerHalfDestination, 0, IntegerSource,
                                                      (1U << 21) | (3U << 10));
constexpr auto FloatUp = FixtureInstruction(0x306, FloatDestination, 0, FloatSource,
                                             (1U << 21) | (2U << 14) | (1U << 12) |
                                                 (1U << 10));
constexpr auto HalfTrunc = FixtureInstruction(0x307, 37, 0, 15, HalfRoundTowardZero);
constexpr auto HalfRound = FixtureInstruction(0x307, 63, 0, 42, HalfRoundToNearest);
constexpr auto UniformPack = FixtureInstruction(0xc3e, PackDestination, 255, PackUniform,
                                                 (1U << 27) | 255U);
constexpr auto ScopedLoad = FixtureInstruction(0x981, ScopedAddress, ScopedAddress,
                                                ScopedPolicyUniform,
                                                WeakDescriptorLoad | ScopedOrdering);

constexpr u32 ZeroRegister = 255;
constexpr u32 AlwaysPredicate = 7;
constexpr u32 SourceTrue = AlwaysPredicate << 23;
constexpr u32 SourceFalse = (AlwaysPredicate | 8U) << 23;
constexpr u32 DiscardPredicate = AlwaysPredicate << 17;
constexpr u32 DiscardSecondPredicate = AlwaysPredicate << 20;
constexpr u32 UniformMode = 1U << 27;
constexpr u32 SignedOperands = 1U << 9;
constexpr auto Exit = FixtureInstruction(0x94d, 0, 0, 0, SourceTrue);
constexpr u32 BankSeedUniform = 2;
constexpr u32 BankSeedMirror = UniformMirror + BankSeedUniform;
constexpr u32 BankSeedOffset = 0x134;
constexpr u32 ShadowMoveOffset = 0x24c;
constexpr u32 ShadowZeroDest = 232;
constexpr u32 ShadowCompareInput = 243;
constexpr u32 CompareConstantOffset = 0x314;
constexpr u32 ByteLoadUniform = 13;
constexpr u32 MultiplyA = 16;
constexpr u32 MultiplyB = 4;
constexpr u32 MultiplyC = 20;
constexpr u32 MultiplyDest = 11;
constexpr u32 CopyShared = 25;
constexpr u32 CopyGlobal = 34;
constexpr u32 MinSource = 107;
constexpr u32 MinUniform = 9;
constexpr u32 MinShadow = UniformMirror + MinUniform;
constexpr u32 PairShadow = 226;
constexpr u32 RegisterMultiplyDest = 41;
constexpr u32 RegisterMultiplyA = 5;
constexpr u32 SelectShadow = 231;
constexpr u32 SelectPredicate = 4;
constexpr u32 PredicateInput = 229;
constexpr u32 PredicateMask = 0x34;
constexpr u32 ImmediateSelectShadow = 237;
constexpr u32 SelectImmediate = 27;
constexpr u32 NonzeroShadow = 240;

constexpr auto UniformBank =
    FixtureInstruction(0xab9, BankSeedUniform, 0, BankSeedOffset << 6, 4U << 9);
constexpr auto ShadowMove =
    FixtureInstruction(0xa02, BankSeedMirror, 0, ShadowMoveOffset << 6, 15U << 8);
constexpr auto ShadowZero =
    FixtureInstruction(0x224, ShadowZeroDest, ZeroRegister, ZeroRegister,
                       SourceFalse | DiscardPredicate | ZeroRegister);
constexpr auto ShadowCompare =
    FixtureInstruction(0xa0c, 0, ShadowCompareInput, CompareConstantOffset << 6,
                       SourceTrue | DiscardSecondPredicate | (6U << 12) | SignedOperands);
constexpr auto ByteLoad = FixtureInstruction(0xab9, ByteLoadUniform, 0, 0x264U << 6, 0);
constexpr auto UniformMultiply =
    FixtureInstruction(0x2a4, MultiplyDest, MultiplyA, MultiplyB,
                       UniformMode | SourceFalse | DiscardPredicate | SignedOperands | MultiplyC,
                       0);

// LDGSTS.E.BYPASS.128 with a policy descriptor and non-zero global/shared displacements.
constexpr auto AsyncCopy =
    FixtureInstruction(0xfae, CopyShared, CopyGlobal, (1U << 16) | 12U,
                       UniformMode | (1U << 20) | (3U << 23) | (1U << 12) |
                           (6U << 9) | (1U << 8) | (1U << 6) | 12U);
constexpr auto ShadowMin = FixtureInstruction(0xc17, MinShadow, MinSource, MinUniform,
                                               UniformMode | SourceTrue | SignedOperands);
constexpr auto ShadowPairZero =
    FixtureInstruction(0x805, PairShadow, 0, 0, (1U << 16) | (ZeroRegister << 8));
constexpr auto ShadowMultiply =
    FixtureInstruction(0xa24, RegisterMultiplyDest, RegisterMultiplyA, CompareConstantOffset << 6,
                       SourceFalse | DiscardPredicate | SignedOperands | ShadowCompareInput);
constexpr auto ShadowSelect = FixtureInstruction(0x207, SelectShadow, SelectShadow, ZeroRegister,
                                                  SelectPredicate << 23);
constexpr auto ShadowPredicates = FixtureInstruction(0x804, 0, PredicateInput, PredicateMask, 0);
constexpr u32 ZeroUniform = 63;
constexpr u32 IgnoreCarry = 15;
constexpr u32 UniformPredicates = 239;
constexpr Instruction UniformAddCarry(u32 dst, u32 a, u32 b, u32 c, u32 carry_out_a,
                                      u32 carry_out_b) {
    // Both input predicates are !UPT. Carry destinations are at bits 81 and 84.
    return FixtureInstruction(0x290, dst, a, b,
                              UniformMode | SourceFalse | (IgnoreCarry << 13) |
                                  (carry_out_a << 17) | (carry_out_b << 20) | c);
}

constexpr Instruction UniformAddExtended(u32 dst, u32 a, u32 b, u32 c, u32 carry_in_a,
                                         u32 carry_in_b) {
    return FixtureInstruction(0x290, dst, a, b,
                              UniformMode | (carry_in_b << 23) | DiscardSecondPredicate |
                                  DiscardPredicate | (carry_in_a << 13) | (1U << 10) | c);
}

constexpr auto UniformCarry = UniformAddCarry(12, 21, 17, ZeroUniform, 3, AlwaysPredicate);
constexpr auto UniformTwoCarries = UniformAddCarry(6, 18, 13, 19, 3, 6);
constexpr auto UniformExtendedCarry = UniformAddExtended(15, 11, 20, ZeroUniform, IgnoreCarry, 4);
constexpr auto UniformExtendedTwoCarries = UniformAddExtended(10, 7, 16, 12, 2, 5);
constexpr auto ImmediateSelect =
    FixtureInstruction(0x807, ImmediateSelectShadow, ZeroRegister, SelectImmediate, 8U << 23);
constexpr auto RegisterCompare =
    FixtureInstruction(0x20c, 0, NonzeroShadow, ZeroRegister,
                       SourceTrue | DiscardSecondPredicate | (5U << 12) | SignedOperands);
constexpr u32 ShuffleDestination = 27;
constexpr u32 ShuffleSource = 12;
constexpr u32 ShuffleLane = 19;
constexpr u32 ShufflePredicate = 5;
constexpr u32 ShuffleMask = 0x170f;
constexpr auto ShuffleDown = FixtureInstruction(0x589, ShuffleDestination, ShuffleSource,
                                                ShuffleLane | (ShuffleMask << 8) | (2U << 26),
                                                ShufflePredicate << 17);
constexpr u32 ImmediateRoundUp = 2;
constexpr u32 ImmediateRoundTowardZero = 3;
constexpr u32 UnsignedToFloat = (1U << 21) | (1U << 12);
constexpr auto ImmediateFloatUp = FixtureInstruction(0x906, 24, 0, 37,
                                                      UnsignedToFloat | (ImmediateRoundUp << 14));
constexpr auto ImmediateFloatTrunc =
    FixtureInstruction(0x845, 4, 0, 260, UnsignedToFloat | (ImmediateRoundTowardZero << 14));

f64 HalfValue(u16 bits) {
    const u32 exponent = (bits >> 10) & 31, fraction = bits & 1023;
    const f64 magnitude = exponent == 31 ? (fraction ? std::numeric_limits<f64>::quiet_NaN()
                                                     : std::numeric_limits<f64>::infinity())
                                         : std::ldexp(f64(exponent ? fraction + 1024 : fraction),
                                                      exponent ? int(exponent) - 25 : -24);
    return bits & 0x8000 ? -magnitude : magnitude;
}

u32 RoundedHalfInteger(u16 bits) {
    const u32 exponent = (bits >> 10) & 31, fraction = bits & 1023;
    if (exponent == 31)
        return fraction ? 0U : bits & 0x8000 ? 0x80000000U : 0x7fffffffU;
    const u32 mantissa = exponent ? fraction + 1024 : fraction;
    u32 magnitude;
    if (exponent >= 25) {
        magnitude = mantissa << (exponent - 25);
    } else {
        const u32 shift = exponent ? 25 - exponent : 24;
        magnitude = mantissa >> shift;
        const u32 remainder = mantissa & ((1U << shift) - 1), midpoint = 1U << (shift - 1);
        magnitude += remainder > midpoint || (remainder == midpoint && (magnitude & 1));
    }
    return bits & 0x8000 ? 0U - magnitude : magnitude;
}

u16 IntegerHalf(s32 input) {
    const u32 sign = input < 0 ? 0x8000U : 0U;
    const u32 magnitude = input < 0 ? 0U - u32(input) : u32(input);
    if (magnitude == 0)
        return 0;
    if (magnitude >= 65520)
        return u16(sign | 0x7c00U);
    u32 exponent = 31U - std::countl_zero(magnitude);
    u32 mantissa = magnitude << (exponent < 10 ? 10 - exponent : 0);
    if (exponent > 10) {
        const u32 shift = exponent - 10, midpoint = 1U << (shift - 1);
        mantissa = magnitude >> shift;
        const u32 remainder = magnitude & ((1U << shift) - 1);
        mantissa += remainder > midpoint || (remainder == midpoint && (mantissa & 1));
        if (mantissa == 2048) {
            mantissa = 1024;
            ++exponent;
        }
    }
    return u16(sign | ((exponent + 15) << 10) | (mantissa & 1023));
}

u16 FloatHalf(f32 input) {
    static const auto values = [] {
        std::array<f64, 31744> result{};
        for (u32 bits = 0; bits < result.size(); ++bits)
            result[bits] = HalfValue(u16(bits));
        return result;
    }();
    if (std::isnan(input))
        return 0x7e00;
    const u32 sign = std::signbit(input) ? 0x8000U : 0U;
    const f64 magnitude = std::abs(f64(input));
    if (magnitude >= 65520.0)
        return u16(sign | 0x7c00U);
    if (magnitude > 65504.0)
        return u16(sign | 0x7bffU);
    const auto upper = std::lower_bound(values.begin(), values.end(), magnitude);
    u32 bits = u32(upper - values.begin());
    if (bits && *upper != magnitude) {
        const f64 low = magnitude - values[bits - 1], high = *upper - magnitude;
        bits -= low < high || (low == high && (bits & 1));
    }
    return u16(sign | bits);
}

u32 Address(u32 index) {
    return 0x50 + index * 8 + ((index + 1) / 3) * 8;
}

struct LoggedMachine {
    std::array<std::array<u32, 256>, 3> registers{};
    std::array<bool, 8> predicates{true, false, true, false, true, false, true, true};
    std::array<u8, 65536> constants{};
    std::unordered_map<u64, u32> global;
    std::unordered_map<u32, u32> shared;
    u32 reads{}, writes{};
    bool carry{};

    LoggedMachine() {
        for (u32 bank = 0; bank < registers.size(); ++bank)
            for (u32 reg = 0; reg < 255; ++reg)
                registers[bank][reg] = 0x9e370001U * (1 + reg + bank * 256);
        for (u32 i = 0; i < constants.size(); ++i)
            constants[i] = (i * 131 + 0x83) & 255;
    }

    u32 Read(u32 bank, u32 reg) const {
        return reg == 255 ? 0U : registers[bank][reg];
    }

    u32 Constant(u32 offset, u32 bytes) const {
        REQUIRE(offset + bytes <= constants.size());
        u32 result = 0;
        for (u32 i = 0; i < bytes; ++i)
            result |= u32{constants[offset + i]} << (i * 8);
        return result;
    }

    bool Predicate(u32 guard) const {
        return predicates[guard & 7] != bool(guard & 8);
    }

    void Run(std::span<const u64> code) {
        for (u32 pc = 0; pc < code.size(); ++pc) {
            const u64 word = code[pc];
            const auto bits = [&](u32 bit, u32 count) {
                return static_cast<u32>((word >> bit) & ((u64{1} << count) - 1));
            };
            if (!Predicate(bits(16, 4)))
                continue;
            const u32 dest = bits(0, 8), a = bits(8, 8), b = bits(20, 8);
            const auto write = [&](u32 bank, u32 value) {
                if (dest != 255)
                    registers[bank][dest] = value;
            };
            if ((word >> 48) == 0x5c98) {
                const bool banked = bits(44, 4) == 10;
                write(banked ? bits(8, 2) : 0, Read(banked ? bits(10, 2) : 0, b));
            } else if ((word >> 48) == 0x4c98) {
                REQUIRE(bits(34, 5) == 0);
                write(0, Constant(bits(20, 14) * 4, 4));
            } else if ((word & 0xfff0000000000000ULL) == 0xef90000000000000ULL) {
                REQUIRE(bits(36, 5) == 0);
                REQUIRE(bits(48, 3) == 0);
                const s32 offset = static_cast<s32>(bits(20, 16) << 16) >> 16;
                write(0, Constant(Read(0, a) + offset, 1));
            } else if ((word >> 56) == 1) {
                write(0, bits(20, 32));
            } else if ((word >> 48) == 0x38e8) {
                const u32 mask = bits(20, 7);
                u32 packed = 0;
                for (u32 i = 0; i < 7; ++i)
                    packed |= u32{predicates[i]} << i;
                write(0, (Read(0, a) & ~mask) | (packed & mask));
            } else if ((word >> 48) == 0x38f0) {
                const u32 value = Read(0, a), mask = bits(20, 7);
                for (u32 i = 0; i < 7; ++i)
                    if (mask & (1U << i))
                        predicates[i] = (value >> i) & 1;
            } else if ((word & 0xfff0000000000000ULL) == 0x5b60000000000000ULL) {
                REQUIRE((bits(49, 3) == 1 || bits(49, 3) == 5 || bits(49, 3) == 6));
                REQUIRE(bits(45, 2) == 0);
                REQUIRE(bits(0, 3) == 7);
                const bool result = bits(49, 3) == 1 ? Read(0, a) < Read(0, b)
                                    : bits(49, 3) == 5 ? Read(0, a) != Read(0, b)
                                    : bits(48, 1)    ? s32(Read(0, a)) >= s32(Read(0, b))
                                                     : Read(0, a) >= Read(0, b);
                const u32 output = bits(3, 3);
                if (output != 7)
                    predicates[output] = result && Predicate(bits(39, 4));
            } else if ((word & 0xff00000000000000ULL) == 0x5b00000000000000ULL) {
                REQUIRE(bits(50, 3) == 0);
                const u32 lhs = (Read(0, a) >> (bits(53, 1) ? 16 : 0)) & 0xffff;
                const u32 rhs = (Read(0, b) >> (bits(35, 1) ? 16 : 0)) & 0xffff;
                write(0, lhs * rhs + Read(0, bits(39, 8)));
            } else if (((word >> 48) & 0xfffe) == 0x5c20) {
                REQUIRE(bits(48, 1) == 1);
                const s32 lhs = Read(0, a), rhs = Read(0, b);
                write(0, Predicate(bits(39, 4)) ? (std::min)(lhs, rhs) : (std::max)(lhs, rhs));
            } else if ((word >> 48) == 0x5ca0) {
                write(0, Predicate(bits(39, 4)) ? Read(0, a) : Read(0, b));
            } else if ((word >> 48) == 0x5ca8) {
                REQUIRE(bits(8, 2) == 2);
                REQUIRE(bits(10, 2) == 1);
                const u16 half = Read(0, b) >> (bits(41, 1) ? 16 : 0);
                write(0, std::bit_cast<u32>(f32(HalfValue(half))));
            } else if ((word & 0xff80000000000000ULL) == 0x5800000000000000ULL) {
                REQUIRE(bits(39, 3) == 7);
                REQUIRE(bits(45, 3) == 0);
                f32 lhs = std::bit_cast<f32>(Read(0, a));
                const f32 rhs = std::bit_cast<f32>(Read(0, b));
                if (bits(54, 1))
                    lhs = std::abs(lhs);
                REQUIRE((bits(48, 4) == 1 || bits(48, 4) == 4));
                write(0, (bits(48, 4) == 1 ? lhs < rhs : lhs > rhs) ? 0xffffffffU : 0U);
            } else if (((word >> 48) & 0xfff8) == 0x5c40) {
                REQUIRE(bits(39, 2) == 0);
                REQUIRE(bits(43, 5) == 0);
                REQUIRE(bits(48, 3) == 7);
                REQUIRE(bits(41, 2) <= 1);
                write(0, bits(41, 2) ? Read(0, a) | Read(0, b) : Read(0, a) & Read(0, b));
            } else if ((word >> 48) == 0x5cb0) {
                REQUIRE(bits(8, 2) == 2);
                REQUIRE(bits(10, 2) == 2);
                REQUIRE(bits(12, 1) == 1);
                REQUIRE(bits(39, 2) <= 1);
                const f64 input = std::bit_cast<f32>(Read(0, b));
                REQUIRE(std::isfinite(input));
                REQUIRE((input >= -2147483648.0 && input < 2147483648.0));
                const f64 floor = std::floor(input), fraction = input - floor;
                const f64 rounded = fraction < 0.5                 ? floor
                                    : fraction > 0.5               ? floor + 1
                                    : std::fmod(floor, 2.0) == 0.0 ? floor
                                                                   : floor + 1;
                write(0, u32(s32(bits(39, 2) == 1 ? floor : rounded)));
            } else if (((word >> 48) & 0xfffc) == 0x5c10) {
                REQUIRE(bits(48, 2) == 0);
                const u64 sum = u64{Read(0, a)} + Read(0, b) + (bits(43, 1) && carry);
                write(0, static_cast<u32>(sum));
                if (bits(47, 1))
                    carry = sum >> 32;
            } else if ((word >> 56) == 0x1c) {
                write(0, Read(0, a) + static_cast<u32>(bits(20, 32)));
            } else if (((word >> 48) & 0xfffe) == 0x5c18) {
                write(0, (Read(0, a) << bits(39, 5)) + Read(0, b));
            } else if ((word & 0xfff0000000000000ULL) == 0xe240000000000000ULL) {
                const s32 offset = static_cast<s32>(bits(20, 24) << 8) >> 8;
                const u32 target = Address(pc) + 8 + offset;
                const u32 start = pc;
                while (pc < code.size() && Address(pc) != target)
                    ++pc;
                REQUIRE(pc > start);
                REQUIRE(pc < code.size());
                --pc;
            } else if ((word & 0xfff0000000000000ULL) == 0xeed0000000000000ULL) {
                REQUIRE(bits(45, 1) == 1);
                REQUIRE(bits(48, 3) == 6);
                const s32 offset = static_cast<s32>(bits(20, 24) << 8) >> 8;
                const u64 address = Read(0, a) | (u64{Read(0, a + 1)} << 32);
                for (u32 i = 0; i < 4; ++i) {
                    const u64 source = address + static_cast<u64>(offset) + i * 4;
                    REQUIRE(global.contains(source));
                    registers[0][dest + i] = global.at(source);
                    ++reads;
                }
            } else if ((word & 0xfff8000000000000ULL) == 0xef58000000000000ULL) {
                REQUIRE(bits(48, 3) == 6);
                const s32 offset = static_cast<s32>(bits(20, 24) << 8) >> 8;
                for (u32 i = 0; i < 4; ++i) {
                    shared[Read(0, a) + offset + i * 4] = Read(0, dest + i);
                    ++writes;
                }
            } else {
                REQUIRE(word == 0xe30000000007000fULL);
                return;
            }
        }
    }
};

class HalfEnvironment final : public Shader::Environment {
public:
    bool native = true;
    bool HasNativeInstructions() const override {
        return native;
    }
    u64 ReadInstruction(u32) override {
        throw std::logic_error("Unexpected instruction read");
    }
    u32 ReadCbufValue(u32, u32) override {
        throw std::logic_error("Unexpected constant read");
    }
    Shader::TextureType ReadTextureType(u32) override {
        return Shader::TextureType::Color2D;
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return Shader::TexturePixelFormat::A8B8G8R8_UNORM;
    }
    bool IsTexturePixelFormatInteger(u32) override {
        return false;
    }
    u32 ReadViewportTransformState() override {
        return 1;
    }
    u32 TextureBoundBuffer() const override {
        return 0;
    }
    u32 LocalMemorySize() const override {
        return 0;
    }
    u32 SharedMemorySize() const override {
        return 0;
    }
    std::array<u32, 3> WorkgroupSize() const override {
        return {1, 1, 1};
    }
    bool HasHLEMacroState() const override {
        return false;
    }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}
};

namespace IR = Shader::IR;

struct HalfIR {
    Shader::ObjectPool<IR::Inst> pool{256};
    IR::Block block{pool};
    HalfEnvironment env;
    std::array<u32, IR::NUM_REGS> registers{};
    std::unordered_map<const IR::Inst*, u64> values;

    explicit HalfIR(std::span<const u64> code) {
        Shader::Maxwell::TranslatorVisitor visitor{env, block};
        for (const u64 word : code) {
            if ((word >> 48) == 0x5ca8)
                visitor.F2F_reg(word);
            else if ((word >> 48) == 0x5cb0)
                visitor.F2I_reg(word);
            else if ((word >> 48) == 0x5cb8)
                visitor.I2F_reg(word);
            else if ((word >> 48) == 0x5c60)
                visitor.FMNMX_reg(word);
            else if ((word >> 48) == 0x5c98)
                visitor.MOV_reg(word);
            else if ((word >> 48) == 0x5c28)
                visitor.SHR_reg(word);
            else if ((word >> 48) == 0x38f8)
                visitor.SHF_r_imm(word);
            else if ((word >> 48) == 0x5c48)
                visitor.SHL_reg(word);
            else if ((word >> 48) == 0x5c18)
                visitor.ISCADD_reg(word);
            else if (((word >> 48) & 0xfffc) == 0x5c10)
                visitor.IADD_reg(word);
            else if ((word & 0xfc00000000000000ULL) == 0x1c00000000000000ULL)
                visitor.IADD32I(word);
            else if ((word >> 58) == 1)
                visitor.LOP32I(word);
            else if ((word >> 56) == 1)
                visitor.MOV32I(word);
            else if ((word & 0xff80000000000000ULL) == 0x5800000000000000ULL)
                visitor.FSET_reg(word);
            else if (((word >> 48) & 0xfff8) == 0x5c40)
                visitor.LOP_reg(word);
            else
                REQUIRE(word == 0xe30000000007000fULL);
        }
    }

    u64 Value(IR::Value value) const {
        if (!value.IsImmediate())
            return values.at(value.Inst());
        switch (value.Type()) {
        case IR::Type::U1:
            return value.U1();
        case IR::Type::U32:
            return value.U32();
        case IR::Type::F32:
            return std::bit_cast<u32>(value.F32());
        default:
            throw std::logic_error("Unexpected half IR immediate");
        }
    }

    f64 Float(IR::Value value) const {
        return value.Type() == IR::Type::F16 ? HalfValue(u16(Value(value)))
                                             : f64(std::bit_cast<f32>(u32(Value(value))));
    }

    u64 Evaluate(const IR::Inst& inst) const {
        const auto a = [&](size_t index) { return Value(inst.Arg(index)); };
        const auto f = [&](size_t index) { return Float(inst.Arg(index)); };
        const auto fp = [](f64 value) { return std::bit_cast<u32>(f32(value)); };
        const auto half_fp = [&](f64 value) -> u16 {
            if (std::isnan(value))
                return 0x7e00;
            if (std::isinf(value))
                return std::signbit(value) ? 0xfc00 : 0x7c00;
            if (value == 0)
                return std::signbit(f(0)) ? 0x8000 : 0;
            return IntegerHalf(s32(value));
        };
        switch (inst.GetOpcode()) {
        case IR::Opcode::Void:
            return 0;
        case IR::Opcode::Identity:
            return a(0);
        case IR::Opcode::GetRegister:
            return registers.at(static_cast<size_t>(inst.Arg(0).Reg()));
        case IR::Opcode::BitCastF32U32:
        case IR::Opcode::BitCastU32F32:
            return a(0);
        case IR::Opcode::UnpackFloat2x16:
            return (a(0) & 65535) | (a(0) >> 16) << 32;
        case IR::Opcode::CompositeExtractF16x2:
            return u32(a(0) >> (a(1) * 32));
        case IR::Opcode::ConvertF32F16:
            return fp(f(0));
        case IR::Opcode::ConvertF16F32:
            return std::isnan(f(0)) ? 0 : FloatHalf(f32(f(0)));
        case IR::Opcode::ConvertF32S32:
            return fp(f32(s32(a(0))));
        case IR::Opcode::ConvertU32F32:
            if (!std::isfinite(f(0)) || f(0) < 0 || f(0) >= 4294967296.0)
                throw std::domain_error("Out-of-range production IR float-to-unsigned conversion");
            return u32(f(0));
        case IR::Opcode::ConvertF16S32:
            return IntegerHalf(s32(a(0)));
        case IR::Opcode::CompositeConstructF16x2:
            return a(0) | (a(1) << 32);
        case IR::Opcode::PackFloat2x16:
            return (a(0) & 65535) | ((a(0) >> 32) << 16);
        case IR::Opcode::FPIsNan32:
            return std::isnan(f(0));
        case IR::Opcode::FPOrdGreaterThan32:
            return f(0) > f(1);
        case IR::Opcode::FPOrdLessThan32:
            return f(0) < f(1);
        case IR::Opcode::FPOrdGreaterThanEqual32:
            return f(0) >= f(1);
        case IR::Opcode::FPMin32:
            return fp(std::fmin(f(0), f(1)));
        case IR::Opcode::FPMax32:
            return fp(std::fmax(f(0), f(1)));
        case IR::Opcode::FPAbs32:
            return fp(std::abs(f(0)));
        case IR::Opcode::FPNeg32:
            return fp(-f(0));
        case IR::Opcode::FPFloor32:
            return fp(std::floor(f(0)));
        case IR::Opcode::FPCeil32:
            return fp(std::ceil(f(0)));
        case IR::Opcode::FPTrunc32:
            return fp(std::trunc(f(0)));
        case IR::Opcode::FPTrunc16:
            return half_fp(std::trunc(f(0)));
        case IR::Opcode::FPRoundEven16: {
            if (!std::isfinite(f(0)))
                return half_fp(f(0));
            const f64 low = std::floor(f(0)), fraction = f(0) - low;
            return half_fp(
                fraction < 0.5 || (fraction == 0.5 && std::fmod(low, 2.0) == 0) ? low : low + 1);
        }
        case IR::Opcode::FPClamp32:
            if (std::isnan(f(0)))
                throw std::domain_error("NaN reaches the production IR clamp");
            return fp(std::fmin(std::fmax(f(0), f(1)), f(2)));
        case IR::Opcode::FPRoundEven32: {
            const f64 low = std::floor(f(0)), fraction = f(0) - low;
            return fp(fraction < 0.5 || (fraction == 0.5 && std::fmod(low, 2.0) == 0) ? low
                                                                                      : low + 1);
        }
        case IR::Opcode::ConvertS32F32:
            if (!std::isfinite(f(0)) || f(0) < -2147483648.0 || f(0) >= 2147483648.0)
                throw std::domain_error("Out-of-range production IR float-to-integer conversion");
            return u32(s32(f(0)));
        case IR::Opcode::LogicalAnd:
            return a(0) && a(1);
        case IR::Opcode::LogicalOr:
            return a(0) || a(1);
        case IR::Opcode::LogicalNot:
            return !a(0);
        case IR::Opcode::SelectU1:
        case IR::Opcode::SelectU32:
        case IR::Opcode::SelectF32:
            return a(0) ? a(1) : a(2);
        case IR::Opcode::BitwiseAnd32:
            return a(0) & a(1);
        case IR::Opcode::BitwiseOr32:
            return a(0) | a(1);
        case IR::Opcode::BitwiseXor32:
            return a(0) ^ a(1);
        case IR::Opcode::BitwiseNot32:
            return ~u32(a(0));
        case IR::Opcode::ShiftRightLogical32:
            return u32(a(0)) >> (a(1) & 31);
        case IR::Opcode::ShiftLeftLogical32:
            return u32(a(0)) << (a(1) & 31);
        case IR::Opcode::CompositeConstructU32x2:
            return u32(a(0)) | (a(1) << 32);
        case IR::Opcode::CompositeExtractU32x2:
            return u32(a(0) >> (a(1) * 32));
        case IR::Opcode::PackUint2x32:
        case IR::Opcode::UnpackUint2x32:
            return a(0);
        case IR::Opcode::UMin32:
            return std::min(u32(a(0)), u32(a(1)));
        case IR::Opcode::ShiftRightLogical64:
            return a(0) >> (a(1) & 63);
        case IR::Opcode::IEqual:
            return a(0) == a(1);
        case IR::Opcode::INeg32:
            return 0U - u32(a(0));
        case IR::Opcode::IAdd32:
            return u32(a(0) + a(1));
        case IR::Opcode::ISub32:
            return u32(a(0) - a(1));
        case IR::Opcode::ULessThan:
            return u32(a(0)) < u32(a(1));
        case IR::Opcode::UGreaterThan:
            return u32(a(0)) > u32(a(1));
        case IR::Opcode::SLessThan:
            return s32(a(0)) < s32(a(1));
        default:
            throw std::logic_error(std::string{IR::NameOf(inst.GetOpcode())});
        }
    }

    u32 Run(u32 input, u32 source = 2, u32 destination = 8) {
        registers.fill(0xdeadbeef);
        registers[source] = input;
        values.clear();
        for (const auto& inst : block.Instructions()) {
            if (inst.GetOpcode() == IR::Opcode::SetRegister)
                registers.at(static_cast<size_t>(inst.Arg(0).Reg())) = u32(Value(inst.Arg(1)));
            else
                values.emplace(&inst, Evaluate(inst));
        }
        REQUIRE(registers[source] == input);
        return registers[destination];
    }
};
} // namespace

TEST_CASE("Packed float-to-byte conversion clamps before packing", "[video_core][ampere][oot]") {
    constexpr u32 source = 27;
    constexpr u32 destination = 32;
    constexpr u32 register_zero = 255;
    const Instruction convert{{0x243U | (7U << 12) | (destination << 16) | (register_zero << 24),
                               source, register_zero | (1U << 10) | (3U << 14), 0}};
    const auto lowered = LowerToMaxwell(std::array{convert, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    HalfIR translation{lowered.instructions};
    for (const f32 input : {-std::numeric_limits<f32>::infinity(), -255.0f, -0.5f, 0.0f, 0.5f,
                            1.0f, 127.75f, 255.0f, 255.75f, 256.0f,
                            std::numeric_limits<f32>::infinity(),
                            std::numeric_limits<f32>::quiet_NaN()}) {
        const u32 expected = std::isnan(input) ? 0U : u32(std::clamp(input, 0.0f, 255.0f));
        CHECK(translation.Run(std::bit_cast<u32>(input), source, destination) == expected);
    }
}

TEST_CASE("I2F from a uniform register selects any byte", "[video_core][ampere][oot]") {
    constexpr u32 destination = 34;
    constexpr u32 uniform = 13;
    for (u32 byte = 0; byte < 4; ++byte) {
        const Instruction convert{{0xd06U | (7U << 12) | (destination << 16),
                                   uniform | (byte << 28), (1U << 27) | (1U << 12), 0}};
        const auto lowered = LowerToMaxwell(std::array{convert, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        CHECK(lowered.instructions[lowered.source_instruction_offsets[0]] ==
              (0x5cb8000000070000ULL | destination | (u64{2} << 8) |
               (u64{216 + uniform} << 20) | (u64{byte} << 41)));
    }
}

TEST_CASE("F2IP packs two rounded unorm bytes", "[video_core][ampere][oot]") {
    constexpr u32 source_a = 57;
    constexpr u32 source_b = 62;
    constexpr u32 destination = 49;
    constexpr u32 register_zero = 255;
    const Instruction convert{{0x243U | (7U << 12) | (destination << 16) | (source_a << 24),
                               source_b, register_zero | (1U << 10), 0}};
    const auto lowered = LowerToMaxwell(std::array{convert, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    HalfIR translation{lowered.instructions};
    const auto byte = [](f32 input) {
        return std::isnan(input) ? 0U : u32(std::nearbyintf(std::clamp(input, 0.0f, 255.0f)));
    };
    const std::array inputs{-std::numeric_limits<f32>::infinity(), -1.0f, 0.0f, 0.25f, 0.75f, 127.5f,
                            128.5f, 254.75f, 255.0f, 300.0f, std::numeric_limits<f32>::infinity(),
                            std::numeric_limits<f32>::quiet_NaN()};
    for (const f32 a : inputs) {
        for (const f32 b : inputs) {
            CAPTURE(a, b);
            translation.values.clear();
            translation.registers.fill(0xdeadbeef);
            translation.registers[source_a] = std::bit_cast<u32>(a);
            translation.registers[source_b] = std::bit_cast<u32>(b);
            for (const auto& inst : translation.block.Instructions()) {
                if (inst.GetOpcode() == IR::Opcode::SetRegister)
                    translation.registers.at(static_cast<size_t>(inst.Arg(0).Reg())) =
                        u32(translation.Value(inst.Arg(1)));
                else
                    translation.values.emplace(&inst, translation.Evaluate(inst));
            }
            CHECK(translation.registers[destination] == (byte(b) | (byte(a) << 8)));
            CHECK(translation.registers[source_a] == std::bit_cast<u32>(a));
            CHECK(translation.registers[source_b] == std::bit_cast<u32>(b));
        }
    }
    // Other rounding modes and a merge register aren't known forms yet.
    auto modified = convert;
    modified.words[2] = 0x000044ff;
    CHECK_FALSE(LowerToMaxwell(std::array{modified, Exit}));
    modified = convert;
    modified.words[2] = 0x00000406;
    CHECK_FALSE(LowerToMaxwell(std::array{modified, Exit}));
}

TEST_CASE("Uniform multiply adds its immediate operand",
          "[video_core][ampere][oot]") {
    constexpr u32 source = 10;
    constexpr u32 factor = 14;
    constexpr u32 addend = 0x7100;
    constexpr u32 multiply_add_mode = 0x0f8e0200;
    const Instruction multiply{{0x4a4U | (7U << 12) | (source << 16) | (source << 24),
                                addend, multiply_add_mode | factor, 0}};
    const auto lowered = LowerToMaxwell(std::array{multiply, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    for (const u32 a : {0U, 1U, 0x80000000U, 0xffffffffU}) {
        for (const u32 b : {0U, 3U, 0x40000001U, 0xffffffffU}) {
            LoggedMachine machine;
            machine.registers[0][216 + source] = a;
            machine.registers[0][216 + factor] = b;
            const auto predicates = machine.predicates;
            machine.Run(lowered.instructions);
            CHECK(machine.registers[0][216 + source] == a * b + addend);
            CHECK(machine.registers[0][216 + factor] == b);
            CHECK(machine.predicates == predicates);
        }
    }
}

TEST_CASE("64-bit right shifts include both words and clamp at 64",
          "[video_core][ampere][oot]") {
    constexpr u32 shift_register = 67;
    constexpr u32 right_u64 = (1U << 9) | (1U << 12);
    for (const bool uniform : {false, true}) {
        const u32 low = uniform ? 12 : 57;
        const u32 high = uniform ? 18 : 65;
        const u32 destination = uniform ? 15 : 59;
        const u32 base = uniform ? 216 : 0;
        for (const u32 shift : {0U, 1U, 16U, 24U, 31U, 32U, 33U, 63U, 64U, 65U, 0xffffffffU}) {
            const u32 opcode = uniform ? 0x899 : 0x219;
            const u32 uniform_mode = uniform ? 1U << 27 : 0;
            const Instruction instruction{{opcode | (7U << 12) | (destination << 16) | (low << 24),
                                           uniform ? shift : shift_register,
                                           right_u64 | uniform_mode | high, 0}};
            const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
            INFO(lowered.error);
            REQUIRE(lowered);
            HalfIR translation{lowered.instructions};
            for (const u64 input : {0ULL, 0xffffffff00000000ULL, 0x8000000180000000ULL,
                                   0xfedcba9876543210ULL, 0xffffffffffffffffULL}) {
                CAPTURE(uniform, shift, input);
                translation.registers.fill(0xdeadbeef);
                translation.registers[base + low] = u32(input);
                translation.registers[base + high] = u32(input >> 32);
                translation.registers[shift_register] = shift;
                translation.values.clear();
                for (const auto& inst : translation.block.Instructions()) {
                    if (inst.GetOpcode() == IR::Opcode::SetRegister)
                        translation.registers.at(static_cast<size_t>(inst.Arg(0).Reg())) =
                            u32(translation.Value(inst.Arg(1)));
                    else
                        translation.values.emplace(&inst, translation.Evaluate(inst));
                }
                CHECK(translation.registers[base + destination] ==
                      (shift >= 64 ? 0U : u32(input >> shift)));
            }
        }
    }
}

TEST_CASE("Synthetic U64 funnel shifts preserve both source words and destination aliases",
          "[video_core][ampere][oot]") {
    // Independently assemble SHF.R.U64: immediate opcode [0:11], PT [12:15],
    // destination [16:23], low source [24:31], shift [32:63], high source [64:71],
    // U64 mode [73:74] and right direction [76]. Scheduling/control word is zero.
    // Keep the 21-bit shift as one arithmetic boundary, not as a copied instruction.
    constexpr u32 low = 53, high = 61;
    for (const u32 destination : {low, 47U}) {
        for (const u32 shift : {0U, 1U, 21U, 31U, 32U}) {
            const Instruction instruction{{0x819U | (7U << 12) | (destination << 16) | (low << 24),
                                           shift, high | (1U << 9) | (1U << 12), 0}};
            const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
            INFO(lowered.error);
            REQUIRE(lowered);
            HalfIR translation{lowered.instructions};
            for (const u64 input : {0ULL, 0x00000001ffe00000ULL, 0x8000000180000000ULL,
                                   0xfedcba9876543210ULL, 0xffffffffffffffffULL}) {
                CAPTURE(destination, shift, input);
                translation.registers.fill(0xdeadbeef);
                translation.registers[low] = u32(input);
                translation.registers[high] = u32(input >> 32);
                translation.values.clear();
                for (const auto& inst : translation.block.Instructions()) {
                    if (inst.GetOpcode() == IR::Opcode::SetRegister)
                        translation.registers.at(static_cast<size_t>(inst.Arg(0).Reg())) =
                            u32(translation.Value(inst.Arg(1)));
                    else
                        translation.values.emplace(&inst, translation.Evaluate(inst));
                }
                CHECK(translation.registers[destination] == u32(input >> shift));
                CHECK(translation.registers[high] == u32(input >> 32));
                if (destination != low) {
                    CHECK(translation.registers[low] == u32(input));
                }
            }
        }
    }
}

TEST_CASE("Ampere F2I signed byte clamps to the byte range", "[video_core][ampere]") {
    const Instruction convert{{0x00097305, 5, 0x0021e100, 0}};
    const auto lowered = LowerToMaxwell(std::array{convert, Exit});
    REQUIRE(lowered);
    HalfIR translation{lowered.instructions};
    for (const auto& [input, expected] : {
             std::pair{0.0f, 0U}, std::pair{-0.0f, 0U}, std::pair{1.9f, 1U},
             std::pair{-1.9f, 0xffffffffU}, std::pair{127.9f, 127U},
             std::pair{-127.9f, 0xffffff81U}, std::pair{128.0f, 127U},
             std::pair{-128.9f, 0xffffff80U}, std::pair{65536.0f, 127U},
             std::pair{-65536.0f, 0xffffff80U},
             std::pair{std::numeric_limits<f32>::infinity(), 127U},
             std::pair{-std::numeric_limits<f32>::infinity(), 0xffffff80U},
             std::pair{std::numeric_limits<f32>::quiet_NaN(), 0U}}) {
        CAPTURE(input);
        CHECK(translation.Run(std::bit_cast<u32>(input), 5, 9) == expected);
    }
}

TEST_CASE("Ampere signed sixteen-bit F2I truncates and saturates numerically",
          "[video_core][ampere]") {
    const Instruction convert{{0x00157305, 3, 0x0021e900, 0}};
    const auto lowered = LowerToMaxwell(std::array{convert, Exit});
    REQUIRE(lowered);
    HalfIR translation{lowered.instructions};
    for (const auto& [input, expected] : {
             std::pair{0.0f, 0U}, std::pair{1.9f, 1U}, std::pair{-1.9f, 0xffffffffU},
             std::pair{32766.9f, 32766U}, std::pair{32767.9f, 32767U},
             std::pair{32768.0f, 32767U}, std::pair{-32767.9f, 0xffff8001U},
             std::pair{-32768.9f, 0xffff8000U},
             std::pair{std::numeric_limits<f32>::infinity(), 32767U},
             std::pair{-std::numeric_limits<f32>::infinity(), 0xffff8000U}}) {
        CAPTURE(input);
        CHECK(translation.Run(std::bit_cast<u32>(input), 3, 21) == expected);
    }
}

TEST_CASE("Ampere register-absolute F2I applies absolute before rounding and saturation",
          "[video_core][ampere]") {
    const Instruction ceiling{{0x00097305, 0x40000005, 0x0020b000, 0}};
    const Instruction nearest_short{{0x00097305, 0x40000005, 0x00212800, 0}};
    const auto ceil_code = LowerToMaxwell(std::array{ceiling, Exit});
    const auto short_code = LowerToMaxwell(std::array{nearest_short, Exit});
    REQUIRE(ceil_code);
    REQUIRE(short_code);
    HalfIR ceil_translation{ceil_code.instructions};
    HalfIR short_translation{short_code.instructions};
    for (const auto& values : {
             std::array{0.0f, 0.0f, 0.0f}, std::array{-0.0f, 0.0f, 0.0f},
             std::array{-1.25f, 2.0f, 1.0f}, std::array{-1.5f, 2.0f, 2.0f},
             std::array{-2.5f, 3.0f, 2.0f}, std::array{2.5f, 3.0f, 2.0f},
             std::array{-65535.25f, 65536.0f, 65535.0f},
             std::array{-65536.0f, 65536.0f, 65535.0f}}) {
        CAPTURE(values[0]);
        const u32 input = std::bit_cast<u32>(values[0]);
        CHECK(ceil_translation.Run(input, 5, 9) == static_cast<u32>(values[1]));
        CHECK(short_translation.Run(input, 5, 9) == static_cast<u32>(values[2]));
    }
}

TEST_CASE("Ampere high registers preserve uniform mirrors and borrowed quartets",
          "[video_core][ampere][fast_fusion_logged]") {
    for (const auto instruction : {ShadowMove, ShadowZero, ShadowCompare}) {
        DYNAMIC_SECTION("opcode " << instruction.Bits(0, 12)) {
            for (const u32 value : {0U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
                const auto lowered = LowerToMaxwell(std::array{UniformBank, instruction, Exit});
                INFO(lowered.error);
                REQUIRE(lowered);
                LoggedMachine machine;
                machine.registers[1][ShadowCompareInput] = value;
                const auto before = machine.registers;
                const auto predicates = machine.predicates;
                machine.Run(lowered.instructions);
                for (u32 reg = 0; reg < 255; ++reg) {
                    if (reg < 246)
                        CHECK(machine.registers[0][reg] ==
                              (reg == BankSeedMirror ? machine.Constant(BankSeedOffset, 4)
                                                     : before[0][reg]));
                    const u32 expected = instruction.words[0] == ShadowMove.words[0] &&
                                                 reg == BankSeedMirror
                                             ? machine.Constant(ShadowMoveOffset, 4)
                                         : instruction.words[0] == ShadowZero.words[0] &&
                                                   reg == ShadowZeroDest
                                             ? 0U
                                             : before[1][reg];
                    CHECK(machine.registers[1][reg] == expected);
                }
                if (instruction.words[0] == ShadowCompare.words[0]) {
                    CHECK(machine.predicates[0] ==
                          (s32(value) >= s32(machine.Constant(CompareConstantOffset, 4))));
                    for (u32 p = 1; p < 8; ++p)
                        CHECK(machine.predicates[p] == predicates[p]);
                } else {
                    CHECK(machine.predicates == predicates);
                }
            }
        }
    }
}

TEST_CASE("Ampere ULDC loads one unsigned byte without neighboring bits",
          "[video_core][ampere][fast_fusion_logged]") {
    for (u32 offset : {0x264U, 0x265U, 0x266U, 0x267U, 0x7fffU}) {
        auto load = ByteLoad;
        load.words[1] = offset << 6;
        const auto lowered = LowerToMaxwell(std::array{load, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        LoggedMachine machine;
        const auto before = machine.registers[0];
        machine.Run(lowered.instructions);
        for (u32 reg = 0; reg < 246; ++reg)
            CHECK(machine.registers[0][reg] ==
                  (reg == UniformMirror + ByteLoadUniform ? machine.constants[offset] : before[reg]));
    }
    for (u32 offset : {0x8000U, 0xffffU}) {
        auto load = ByteLoad;
        load.words[1] = offset << 6;
        CHECK_FALSE(LowerToMaxwell(std::array{load, Exit}));
    }
}

TEST_CASE("Ampere register shadows distinguish pairs, uniform inputs and constants",
          "[video_core][ampere][fast_fusion_logged]") {
    for (const auto instruction : {ShadowMin, ShadowPairZero, ShadowMultiply}) {
        DYNAMIC_SECTION("opcode " << instruction.Bits(0, 12)) {
            const auto lowered = LowerToMaxwell(std::array{UniformBank, instruction, Exit});
            INFO(lowered.error);
            REQUIRE(lowered);
            LoggedMachine machine;
            machine.registers[0][MinSource] = 0x80000000;
            machine.registers[0][MinShadow] = 9;
            machine.registers[0][RegisterMultiplyA] = 0xffff1234;
            machine.registers[1][ShadowCompareInput] = 0xfedcba98;
            const auto before = machine.registers;
            const auto predicates = machine.predicates;
            machine.Run(lowered.instructions);
            for (u32 reg = 0; reg < 255; ++reg) {
                if (reg < 246) {
                    const u32 expected =
                        reg == BankSeedMirror ? machine.Constant(BankSeedOffset, 4)
                        : instruction.words[0] == ShadowMultiply.words[0] &&
                                  reg == RegisterMultiplyDest
                            ? 0xffff1234U * machine.Constant(CompareConstantOffset, 4) + 0xfedcba98U
                            : before[0][reg];
                    CHECK(machine.registers[0][reg] == expected);
                }
                const u32 expected = instruction.words[0] == ShadowMin.words[0] && reg == MinShadow
                                         ? 0x80000000U
                                     : instruction.words[0] == ShadowPairZero.words[0] &&
                                             (reg == PairShadow || reg == PairShadow + 1)
                                         ? 0U
                                         : before[1][reg];
                CHECK(machine.registers[1][reg] == expected);
            }
            CHECK(machine.predicates == predicates);
        }
    }
}

TEST_CASE("Ampere uniform multiply uses UP0 and restores ordinary predicates",
          "[video_core][ampere][fast_fusion_logged]") {
    for (u32 guard : {0U, 8U}) {
        for (const bool enabled : {false, true}) {
            for (const auto values : {std::array{0U, 0U, 0xffffffffU},
                                      std::array{0x89abcdefU, 0xfedcba98U, 0x12345678U}}) {
                auto multiply = UniformMultiply;
                multiply.words[0] |= guard << 12;
                const auto lowered = LowerToMaxwell(std::array{multiply, Exit});
                INFO(lowered.error);
                REQUIRE(lowered);
                LoggedMachine machine;
                machine.registers[0][UniformMirror + MultiplyA] = values[0];
                machine.registers[0][UniformMirror + MultiplyB] = values[1];
                machine.registers[0][UniformMirror + MultiplyC] = values[2];
                machine.registers[0][239] = enabled ? 0x55U : 0x15U;
                machine.predicates[6] = !enabled;
                const auto before = machine.registers[0];
                const auto predicates = machine.predicates;
                machine.Run(lowered.instructions);
                for (u32 reg = 0; reg < 246; ++reg)
                    CHECK(machine.registers[0][reg] ==
                          (reg == UniformMirror + MultiplyDest && (enabled != bool(guard & 8))
                               ? values[0] * values[1] + values[2]
                               : before[reg]));
                CHECK(machine.predicates == predicates);
            }
        }
    }
}

TEST_CASE("Ampere select and predicate writes read the guest register shadow",
          "[video_core][ampere][fast_fusion_logged]") {
    for (const auto instruction : {ShadowSelect, ShadowPredicates}) {
        DYNAMIC_SECTION("opcode " << instruction.Bits(0, 12)) {
            for (const bool selected : {false, true}) {
                const auto lowered = LowerToMaxwell(std::array{UniformBank, instruction, Exit});
                INFO(lowered.error);
                REQUIRE(lowered);
                LoggedMachine machine;
                constexpr u32 PredicateBits = 0x14;
                machine.registers[1][PredicateInput] = PredicateBits;
                machine.registers[1][SelectShadow] = 0xdeadbeef;
                machine.predicates[SelectPredicate] = selected;
                const auto before = machine.registers;
                const auto predicates = machine.predicates;
                machine.Run(lowered.instructions);
                for (u32 reg = 0; reg < 255; ++reg) {
                    if (reg < 246)
                        CHECK(machine.registers[0][reg] ==
                              (reg == BankSeedMirror ? machine.Constant(BankSeedOffset, 4)
                                                     : before[0][reg]));
                    const u32 expected =
                        instruction.words[0] == ShadowSelect.words[0] && reg == SelectShadow
                            ? (selected ? 0xdeadbeefU : 0U)
                            : before[1][reg];
                    CHECK(machine.registers[1][reg] == expected);
                }
                for (u32 p = 0; p < 8; ++p)
                    CHECK(machine.predicates[p] ==
                          (instruction.words[0] == ShadowPredicates.words[0] &&
                                   (PredicateMask & (1U << p))
                               ? bool(PredicateBits & (1U << p))
                               : predicates[p]));
            }
        }
    }
}

TEST_CASE("Ampere async copy uses its source predicate and full 64-bit address",
          "[video_core][ampere][fast_fusion_logged]") {
    for (u32 guard : {0U, 7U, 8U}) {
        for (u32 enable : {3U, 11U}) {
            for (const bool condition : {false, true}) {
                auto copy = AsyncCopy;
                copy.words[0] = (copy.words[0] & ~0xf000U) | (guard << 12);
                copy.words[2] = (copy.words[2] & ~0x07800000U) | (enable << 23);
                const auto lowered = LowerToMaxwell(std::array{copy, Exit});
                INFO(lowered.error);
                REQUIRE(lowered);
                LoggedMachine machine;
                constexpr u64 GlobalBase = 0x5fffffff4ULL;
                constexpr u64 GlobalAddress = GlobalBase + 12;
                constexpr u32 SharedBase = 144;
                constexpr u32 SharedAddress = SharedBase + 16;
                machine.registers[0][CopyShared] = SharedBase;
                machine.registers[0][CopyGlobal] = static_cast<u32>(GlobalBase);
                machine.registers[0][CopyGlobal + 1] = static_cast<u32>(GlobalBase >> 32);
                machine.predicates[0] = condition;
                machine.predicates[3] = condition;
                const auto before = machine.registers[0];
                const auto predicates = machine.predicates;
                const bool active = machine.Predicate(guard), source = machine.Predicate(enable);
                for (u32 i = 0; i < 4; ++i) {
                    if (active && source)
                        machine.global[GlobalAddress + i * 4] = 0xfeedfaceU + i;
                    machine.shared[SharedAddress + i * 4] = 0xdeadbeef;
                }
                machine.Run(lowered.instructions);
                CHECK(machine.reads == (active && source ? 4U : 0U));
                CHECK(machine.writes == (active ? 4U : 0U));
                for (u32 i = 0; i < 4; ++i)
                    CHECK(machine.shared.at(SharedAddress + i * 4) == (!active  ? 0xdeadbeefU
                                                             : source ? 0xfeedfaceU + i
                                                                      : 0U));
                for (u32 reg = 0; reg < 246; ++reg)
                    CHECK(machine.registers[0][reg] == before[reg]);
                CHECK(machine.predicates == predicates);
            }
        }
    }
}

TEST_CASE("Ampere half conversion rounds every half value to a signed even integer",
          "[video_core][ampere][fast_fusion_logged]") {
    const auto lowered = LowerToMaxwell(std::array{HalfConversion, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    LoggedMachine machine;
    const auto predicates = machine.predicates;
    for (u32 half = 0; half < 65536; ++half) {
        machine.registers[0][HalfSource] = 0xfc000000U | half;
        machine.Run(lowered.instructions);
        CAPTURE(half);
        CHECK(machine.registers[0][HalfDestination] == RoundedHalfInteger(half));
        CHECK(machine.registers[0][HalfSource] == (0xfc000000U | half));
        CHECK(machine.predicates == predicates);
    }
}

TEST_CASE(
    "Ampere uniform carry outputs preserve unrelated UP and ordinary predicates",
    "[video_core][ampere][fast_fusion_logged]") {
    constexpr std::array values{0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU};
    for (const auto instruction : {UniformCarry, UniformTwoCarries}) {
        DYNAMIC_SECTION("mode " << instruction.words[2]) {
            const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
            INFO(lowered.error);
            REQUIRE(lowered);
            const u32 destination = UniformMirror + instruction.Bits(16, 8);
            const u32 source_a = UniformMirror + instruction.Bits(24, 8);
            const u32 source_b = UniformMirror + instruction.Bits(32, 8);
            const u32 source_c = instruction.Bits(64, 8);
            LoggedMachine machine;
            for (u32 a : values) {
                for (u32 b : values) {
                    for (u32 c : values) {
                        for (u32 packed : {0x12340015U, 0x1234006aU}) {
                            machine.registers[0][source_a] = a;
                            machine.registers[0][source_b] = b;
                            if (source_c != ZeroUniform)
                                machine.registers[0][UniformMirror + source_c] = c;
                            machine.registers[0][UniformPredicates] = packed;
                            const auto before = machine.registers[0];
                            const auto predicates = machine.predicates;
                            const u64 sum = u64{a} + b + (source_c != ZeroUniform ? c : 0);
                            u32 expected_predicates = packed;
                            for (u32 i = 0; i < 2; ++i) {
                                const u32 output = instruction.Bits(81 + i * 3, 3);
                                if (output == 7)
                                    continue;
                                const u32 mask = 1U << (6 - output);
                                expected_predicates = (expected_predicates & ~mask) |
                                                      (sum >= (u64{i + 1} << 32) ? mask : 0);
                            }
                            machine.Run(lowered.instructions);
                            CAPTURE(a, b, c, packed);
                            for (u32 reg = 0; reg < 246; ++reg)
                                CHECK(machine.registers[0][reg] == (reg == destination ? u32(sum)
                                                                    : reg == UniformPredicates
                                                                        ? expected_predicates
                                                                        : before[reg]));
                            CHECK(machine.predicates == predicates);
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("Ocarina call links stay proven across a predicate-to-register write",
          "[video_core][ampere][fast_fusion_logged]") {
    std::array<Instruction, 7> code{{
        {{0x00097802, 0x30, 0xf00, 0}},
        {{0xff107803, 0x40, 0, 0}}, // P2R R16, RZ, 0x40
        {{0x00007944, 0x10, 0x03c00000, 0}},
        Exit,
        {{0x845U | (7U << 12) | (21U << 16), 0x183, (1U << 21) | (13U << 12), 0}},
        {{0x00047202, 9, 0xf00, 0}},
        {{0x04007950, 0xffffff90, 0x03c3ffff, 0}},
    }};
    const auto lowered = LowerToMaxwell(code);
    INFO(lowered.error);
    REQUIRE(lowered);
    code[1] = {{0xff097803, 0x40, 0, 0}}; // Writes the link register itself.
    const auto clobbered = LowerToMaxwell(code);
    CHECK_FALSE(clobbered);
    CHECK(clobbered.error == "native call linkage mode");
}

// Independently assembled ISA operands for the three uniform cases below. Field positions
// follow Instruction::Bits consumers in lower_to_maxwell.h; no logged instruction is a seed.
constexpr Instruction UniformArithmetic(u32 opcode, u32 target, u32 source, u32 operand,
                                        u32 controls) {
    return {{opcode | (7U << 12) | (target << 16) | (source << 24), operand, controls, 0}};
}

constexpr Instruction UniformLoad(bool indexed, u32 target, u32 index, u32 bank,
                                  u32 offset, u32 width, u32 guard = 7) {
    // Opcode [0:11], guard [12:15], URd [16:23], URindex [24:31],
    // signed displacement [38:53], bank [54:58], width [73:75], indexed flag [91].
    return {{(indexed ? 0xabbU : 0xab9U) | (guard << 12) | (target << 16) | (index << 24),
             ((offset & 0xffffU) << 6) | (bank << 22),
             (width << 9) | (indexed ? 1U << 27 : 0U), 0}};
}

TEST_CASE("Synthetic uniform LEA carries through any UP and preserves the rest",
          "[video_core][ampere][fast_fusion_logged]") {
    // ULEA UR14, UR3, UR14, 5. Rc=URZ [64:71], shift [75:79],
    // carry destination [81:83], unused carry input [87:90], uniform flag [91].
    constexpr u32 shift = 5, source = 216 + 3, target = 216 + 14;
    constexpr std::array values{0U, 1U, 0x07ffffffU, 0x08000000U, 0xffffffffU};
    for (u32 up = 0; up < 7; ++up) {
        const u32 mask = 1U << (6 - up);
        const auto instruction = UniformArithmetic(
            0x291, 14, 3, 14, 63U | (shift << 11) | (up << 17) | (15U << 23) | (1U << 27));
        const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
        CAPTURE(up);
        INFO(lowered.error);
        REQUIRE(lowered);
        LoggedMachine machine;
        for (u32 a : values) {
            for (u32 b : values) {
                for (u32 packed : {0x12340015U, 0x1234006aU}) {
                    machine.registers[0][source] = a;
                    machine.registers[0][target] = b;
                    machine.registers[0][239] = packed;
                    const auto before = machine.registers[0];
                    const auto predicates = machine.predicates;
                    machine.Run(lowered.instructions);
                    CAPTURE(a, b, packed);
                    const u32 sum = (a << shift) + b;
                    for (u32 reg = 0; reg < 246; ++reg)
                        CHECK(machine.registers[0][reg] ==
                              (reg == target ? sum
                               : reg == 239 ? (packed & ~mask) | (sum < b ? mask : 0)
                                            : before[reg]));
                    CHECK(machine.predicates == predicates);
                }
            }
        }
    }
}

TEST_CASE("Synthetic indexed uniform constant loads keep their U8 and U16 widths",
          "[video_core][ampere][fast_fusion_logged]") {
    struct Case {
        Instruction instruction;
        u64 target, index, offset, bank, width;
    };
    // Arbitrary distinct registers, banks and byte offsets, including repeated source/offset
    // with a different destination. The expected Maxwell words are assembled separately.
    const std::array cases{
        Case{UniformLoad(true, 2, 9, 2, 0x24, 2), 218, 225, 0x24, 2, 2},
        Case{UniformLoad(true, 11, 1, 7, 0x15b, 0), 227, 217, 0x15b, 7, 0},
        Case{UniformLoad(true, 8, 5, 3, 0x0d, 0), 224, 221, 0x0d, 3, 0},
        Case{UniformLoad(true, 16, 1, 7, 0x15b, 0), 232, 217, 0x15b, 7, 0},
    };
    for (const Case& test : cases) {
        CAPTURE(test.instruction.words[0]);
        const auto lowered = LowerToMaxwell(std::array{test.instruction, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        CHECK(lowered.instructions[lowered.source_instruction_offsets[0]] ==
              (0xef90000000070000ULL | test.target | (test.index << 8) | (test.offset << 20) |
               (test.bank << 36) | (test.width << 48)));
    }
    // @UP3 with an index register and offset -3: the guard moves, the signed offset stays.
    const Instruction negative = UniformLoad(true, 13, 4, 9, 0xfffd, 0, 3);
    const auto guarded = LowerToMaxwell(std::array{negative, Exit});
    INFO(guarded.error);
    REQUIRE(guarded);
    const u64 load = 0xef90000000030000ULL | 229 | (u64{220} << 8) |
                     (u64{0xfffd} << 20) | (u64{9} << 36);
    CHECK(std::ranges::find(guarded.instructions, load) != guarded.instructions.end());
    const u64 transfer = 0x38f0000000070000ULL | (u64{239} << 8) | (u64{1U << 3} << 20);
    CHECK(std::ranges::find(guarded.instructions, transfer) != guarded.instructions.end());
    // An indexed load can have any immediate: the register can align UR7 + 0x37.
    const Instruction indexed_word = UniformLoad(true, 15, 7, 6, 0x37, 4);
    const auto word = LowerToMaxwell(std::array{indexed_word, Exit});
    INFO(word.error);
    REQUIRE(word);
    CHECK(word.instructions[word.source_instruction_offsets[0]] ==
          (0xef90000000070000ULL | 231 | (u64{223} << 8) | (u64{0x37} << 20) | (u64{6} << 36) |
           (u64{4} << 48)));
    // A direct U16 load needs an even offset, and a direct subword load the short offset range.
    const Instruction misaligned = UniformLoad(false, 20, 0, 8, 3, 2);
    CHECK_FALSE(LowerToMaxwell(std::array{misaligned, Exit}));
    const Instruction far = UniformLoad(false, 21, 0, 1, 0x8007, 0);
    CHECK_FALSE(LowerToMaxwell(std::array{far, Exit}));
}

TEST_CASE("Synthetic uniform immediate add carries through any UP and preserves the rest",
          "[video_core][ampere][fast_fusion_logged]") {
    // UIADD3 UR18, UR18, 0x35. Rc=URZ, first carry destination [81:83],
    // second carry destination [84:86] disabled, carry input [87:90], .X [74].
    constexpr u32 target = 216 + 18, immediate = 0x35;
    constexpr std::array values{0U, 1U, 0xffffffcaU, 0xffffffcbU, 0xffffffffU};
    for (u32 up = 0; up < 7; ++up) {
        const u32 mask = 1U << (6 - up);
        const u32 shared_controls = 63U | (15U << 13) | (7U << 20) | (1U << 27);
        const auto carry_out = UniformArithmetic(
            0x890, 18, 18, immediate, shared_controls | (up << 17) | (15U << 23));
        const auto carry_in = UniformArithmetic(
            0x890, 18, 18, immediate, shared_controls | (7U << 17) | (1U << 10) | (up << 23));
        const auto lowered_out = LowerToMaxwell(std::array{carry_out, Exit});
        const auto lowered_in = LowerToMaxwell(std::array{carry_in, Exit});
        CAPTURE(up);
        INFO(lowered_out.error);
        INFO(lowered_in.error);
        REQUIRE(lowered_out);
        REQUIRE(lowered_in);
        LoggedMachine machine;
        for (u32 a : values) {
            for (u32 packed : {0x12340015U, 0x1234006aU}) {
                machine.registers[0][target] = a;
                machine.registers[0][239] = packed;
                const auto before = machine.registers[0];
                const auto predicates = machine.predicates;
                machine.Run(lowered_out.instructions);
                CAPTURE(a, packed);
                const bool carried = u64{a} + immediate > 0xffffffffULL;
                for (u32 reg = 0; reg < 246; ++reg)
                    CHECK(machine.registers[0][reg] ==
                          (reg == target ? a + immediate
                           : reg == 239 ? (packed & ~mask) | (carried ? mask : 0)
                                        : before[reg]));
                CHECK(machine.predicates == predicates);

                machine.registers[0][target] = a;
                machine.registers[0][239] = packed;
                machine.Run(lowered_in.instructions);
                CHECK(machine.registers[0][target] == a + immediate + ((packed & mask) != 0 ? 1U : 0U));
                CHECK(machine.registers[0][239] == packed);
                CHECK(machine.predicates == predicates);
            }
        }
    }
}

TEST_CASE(
    "Ampere half conversion keeps production IR conversions finite and in range",
    "[video_core][ampere][fast_fusion_logged]") {
    const auto lowered = LowerToMaxwell(std::array{HalfConversion, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    HalfIR translation{lowered.instructions};
    for (u32 half = 0; half < 65536; ++half) {
        CAPTURE(half);
        CHECK(translation.Run(0xfc000000U | half, HalfSource, HalfDestination) ==
              RoundedHalfInteger(u16(half)));
    }
}

TEST_CASE(
    "Ampere half conversion preserves its execution predicate and other registers",
    "[video_core][ampere][fast_fusion_logged]") {
    for (u32 guard : {0U, 7U, 8U}) {
        auto instruction = HalfConversion;
        instruction.words[0] = (instruction.words[0] & ~0xf000U) | (guard << 12);
        const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        for (const bool condition : {false, true}) {
            for (u32 half : {0x0001U, 0x3e00U, 0xbe00U, 0x7bffU, 0x7c00U, 0xfc00U, 0x7e00U}) {
                LoggedMachine machine;
                machine.registers[0][HalfSource] = 0xfc000000U | half;
                machine.predicates[0] = condition;
                const auto before = machine.registers[0];
                const auto predicates = machine.predicates;
                const bool active = machine.Predicate(guard);
                machine.Run(lowered.instructions);
                for (u32 reg = 0; reg < 246; ++reg)
                    CHECK(machine.registers[0][reg] ==
                          (reg == HalfDestination && active ? RoundedHalfInteger(u16(half))
                                                            : before[reg]));
                CHECK(machine.predicates == predicates);
            }
        }
    }
}

TEST_CASE("Ampere upper half conversion ignores the poisoned lower half",
          "[video_core][ampere][fast_fusion_logged]") {
    auto instruction = HalfConversion;
    instruction.words[1] |= 1U << 28;
    const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    HalfIR translation{lowered.instructions};
    for (u32 half = 0; half < 65536; ++half) {
        CAPTURE(half);
        CHECK(translation.Run((half << 16) | 0xfc00U, HalfSource, HalfDestination) ==
              RoundedHalfInteger(u16(half)));
    }
}

TEST_CASE("Ampere half floor converts every half pattern safely",
          "[video_core][ampere][fast_fusion_logged]") {
    for (const bool high : {false, true}) {
        auto instruction = HalfFloor;
        instruction.words[1] |= u32{high} << 28;
        const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        HalfIR translation{lowered.instructions};
        for (u32 half = 0; half < 65536; ++half) {
            CAPTURE(high, half);
            const u32 expected = (half & 0x7c00) == 0x7c00
                                     ? RoundedHalfInteger(u16(half))
                                     : u32(s32(std::floor(HalfValue(u16(half)))));
            CHECK(translation.Run(high ? (half << 16) | 0xfc00U : 0xfc000000U | half,
                                  FloorSource, FloorDestination) == expected);
        }
    }
}

TEST_CASE("Ampere register comparison reads the high guest shadow",
          "[video_core][ampere][fast_fusion_logged]") {
    const auto lowered = LowerToMaxwell(std::array{UniformBank, RegisterCompare, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    for (u32 input : {0U, 1U, 0x80000000U, 0xffffffffU}) {
        LoggedMachine machine;
        machine.registers[1][NonzeroShadow] = input;
        const auto before = machine.registers;
        const auto predicates = machine.predicates;
        machine.Run(lowered.instructions);
        for (u32 reg = 0; reg < 255; ++reg) {
            if (reg < 246)
                CHECK(machine.registers[0][reg] ==
                      (reg == BankSeedMirror ? machine.Constant(BankSeedOffset, 4)
                                             : before[0][reg]));
            CHECK(machine.registers[1][reg] == before[1][reg]);
        }
        for (u32 p = 0; p < 8; ++p)
            CHECK(machine.predicates[p] == (p == 0 ? input != 0 : predicates[p]));
    }
}

TEST_CASE("Ampere integer to half conversion rounds ties and clears the upper half",
          "[video_core][ampere][fast_fusion_logged]") {
    const auto lowered = LowerToMaxwell(std::array{SignedWordToHalf, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    HalfIR translation{lowered.instructions};
    std::array<f64, 31744> positive;
    for (u32 bits = 0; bits < positive.size(); ++bits)
        positive[bits] = HalfValue(u16(bits));
    const auto check = [&](s32 input) {
        const f64 magnitude = std::abs(f64(input));
        u32 expected = 0;
        if (magnitude >= 65520.0) {
            expected = 0x7c00;
        } else if (magnitude > 65504.0) {
            expected = 0x7bff;
        } else {
            const auto upper = std::lower_bound(positive.begin(), positive.end(), magnitude);
            expected = u32(upper - positive.begin());
            if (expected && *upper != magnitude) {
                const f64 low_distance = magnitude - positive[expected - 1];
                const f64 high_distance = *upper - magnitude;
                expected -= low_distance < high_distance ||
                            (low_distance == high_distance && (expected & 1));
            }
        }
        expected |= input < 0 ? 0x8000U : 0U;
        CAPTURE(input);
        CHECK(translation.Run(u32(input), IntegerSource, IntegerHalfDestination) == expected);
    };
    for (s32 input = -65536; input <= 65536; ++input)
        check(input);
    for (s32 input : {(std::numeric_limits<s32>::min)(), -16777217, 16777217,
                      (std::numeric_limits<s32>::max)()})
        check(input);
}

TEST_CASE(
    "Ampere scoped load uses the full register pointer despite a bound policy descriptor",
    "[video_core][ampere][fast_fusion_logged]") {
    for (s32 offset : {-32, 0, 32}) {
        auto instruction = ScopedLoad;
        instruction.words[1] |= u32(offset) << 8;
        const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        LoggedMachine machine;
        constexpr u64 AddressBase = 0x600000028ULL;
        constexpr u32 Policy = UniformMirror + ScopedPolicyUniform;
        machine.registers[0][ScopedAddress] = static_cast<u32>(AddressBase);
        machine.registers[0][ScopedAddress + 1] = static_cast<u32>(AddressBase >> 32);
        machine.registers[0][Policy] = 0x6ba871de;
        machine.registers[0][Policy + 1] = 0x4320f5c6;
        const auto before = machine.registers[0];
        const auto predicates = machine.predicates;
        for (u32 i = 0; i < 4; ++i)
            machine.global[AddressBase + offset + i * 4] = 0xfeed0000U + i;
        machine.Run(lowered.instructions);
        CHECK(machine.reads == 4);
        for (u32 reg = 0; reg < 246; ++reg)
            CHECK(machine.registers[0][reg] ==
                  (reg >= ScopedAddress && reg < ScopedAddress + 4
                       ? 0xfeed0000U + reg - ScopedAddress
                       : before[reg]));
        CHECK(machine.predicates == predicates);
    }
}

TEST_CASE("Fast Fusion scoped load marks production IR coherent without changing weak descriptors",
          "[video_core][ampere][fast_fusion_logged]") {
    for (const bool scoped : {false, true}) {
        auto instruction = ScopedLoad;
        if (!scoped)
            instruction.words[2] = WeakDescriptorLoad;
        const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
        INFO(lowered.error);
        REQUIRE(lowered);
        const auto word =
            std::find_if(lowered.instructions.begin(), lowered.instructions.end(), [](u64 value) {
                return (value & 0xfff0000000000000ULL) == 0xeed0000000000000ULL;
            });
        REQUIRE(word != lowered.instructions.end());
        CHECK(((*word >> 46) & 3) == u64{scoped});
        for (const bool native : {false, true}) {
            Shader::ObjectPool<IR::Inst> pool{64};
            IR::Block block{pool};
            HalfEnvironment env;
            env.native = native;
            Shader::Maxwell::TranslatorVisitor visitor{env, block};
            visitor.LDG(*word);
            u32 loads = 0;
            for (const auto& inst : block.Instructions()) {
                if (inst.GetOpcode() == IR::Opcode::LoadGlobal128) {
                    ++loads;
                    CHECK(inst.Flags<bool>() == (scoped && native));
                }
                CHECK(inst.GetOpcode() != IR::Opcode::DeviceMemoryBarrier);
                CHECK(inst.GetOpcode() != IR::Opcode::WorkgroupMemoryBarrier);
            }
            CHECK(loads == 1);
        }
    }
}

TEST_CASE("Ampere half rounds preserve special values and clear the upper half",
          "[video_core][ampere][fast_fusion_logged]") {
    for (const auto instruction : {HalfTrunc, HalfRound}) {
        DYNAMIC_SECTION("mode " << instruction.words[2]) {
            for (const bool high : {false, true}) {
                auto selected = instruction;
                selected.words[1] |= u32{high} << 28;
                const auto lowered = LowerToMaxwell(std::array{selected, Exit});
                INFO(lowered.error);
                REQUIRE(lowered);
                HalfIR translation{lowered.instructions};
                for (u32 half = 0; half < 65536; ++half) {
                    const u32 output =
                        translation.Run(high ? (half << 16) | 0xfc00U : 0xfc000000U | half,
                                        instruction.Bits(32, 8), instruction.Bits(16, 8));
                    CAPTURE(high, half);
                    CHECK((output >> 16) == 0);
                    if ((half & 0x7c00) == 0x7c00) {
                        if (half & 1023)
                            CHECK(std::isnan(HalfValue(u16(output))));
                        else
                            CHECK(output == half);
                    } else {
                        const f64 expected = instruction.words[2] == HalfRoundTowardZero
                                                 ? std::trunc(HalfValue(u16(half)))
                                                 : f64(s32(RoundedHalfInteger(u16(half))));
                        CHECK(HalfValue(u16(output)) == expected);
                        CHECK((output & 0x8000) == (half & 0x8000));
                    }
                }
            }
        }
    }
}

TEST_CASE(
    "Ampere zero-high uniform pack preserves half rounding and exceptional values",
    "[video_core][ampere][fast_fusion_logged]") {
    const auto lowered = LowerToMaxwell(std::array{UniformPack, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    HalfIR translation{lowered.instructions};
    u32 nan_checks = 0;
    bool repaired_output = false;
    for (const auto& inst : translation.block.Instructions()) {
        nan_checks += inst.GetOpcode() == IR::Opcode::FPIsNan32;
        if (inst.GetOpcode() == IR::Opcode::SetRegister &&
            static_cast<u32>(inst.Arg(0).Reg()) == PackDestination)
            repaired_output = inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::BitwiseOr32;
    }
    CHECK(nan_checks > 0);
    CHECK(repaired_output);
    for (const auto& [input, expected] :
         {std::pair{0x00000000U, 0x0000U}, std::pair{0x80000000U, 0x8000U},
          std::pair{0x3f800000U, 0x3c00U}, std::pair{0x3fc00000U, 0x3e00U},
          std::pair{0x3f801000U, 0x3c00U}, std::pair{0x3f803000U, 0x3c02U},
          std::pair{0x477fe000U, 0x7bffU}, std::pair{0x477ff000U, 0x7c00U},
          std::pair{0x33800000U, 0x0001U}, std::pair{0x33000000U, 0x0000U},
          std::pair{0xb3800000U, 0x8001U}, std::pair{0xb3000000U, 0x8000U},
          std::pair{0x7f800000U, 0x7c00U}, std::pair{0xff800000U, 0xfc00U}}) {
        CAPTURE(input);
        CHECK(translation.Run(input, UniformMirror + PackUniform, PackDestination) == expected);
    }
    for (u32 input : {0x7fc00000U, 0xffc12345U, 0x7f800001U}) {
        const u32 output = translation.Run(input, UniformMirror + PackUniform, PackDestination);
        CHECK((output >> 16) == 0);
        CHECK(std::isnan(HalfValue(u16(output))));
    }
}

TEST_CASE("Ampere signed integer to float rounds toward positive infinity",
          "[video_core][ampere][fast_fusion_logged]") {
    const auto lowered = LowerToMaxwell(std::array{FloatUp, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    HalfIR translation{lowered.instructions};
    u32 round_trips = 0, ordinary_conversions = 0;
    for (const auto& inst : translation.block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::ConvertU32F32) {
            ++round_trips;
            CHECK(inst.Arg(0).Inst()->GetOpcode() == IR::Opcode::FPAbs32);
        }
        if (inst.GetOpcode() == IR::Opcode::ConvertF32S32) {
            ++ordinary_conversions;
            CHECK(inst.Flags<IR::FpControl>().rounding == IR::FpRounding::DontCare);
        }
        CHECK(inst.GetOpcode() != IR::Opcode::ConvertS32F32);
    }
    CHECK(round_trips == 1);
    CHECK(ordinary_conversions == 1);
    for (const auto& [input, expected] :
         {std::pair{0, 0x00000000U}, std::pair{1, 0x3f800000U}, std::pair{-1, 0xbf800000U},
          std::pair{16777215, 0x4b7fffffU}, std::pair{16777217, 0x4b800001U},
          std::pair{16777219, 0x4b800002U}, std::pair{-16777217, 0xcb800000U},
          std::pair{-16777219, 0xcb800001U}, std::pair{67108865, 0x4c800001U},
          std::pair{-67108865, 0xcc800000U},
          std::pair{(std::numeric_limits<s32>::min)(), 0xcf000000U},
          std::pair{(std::numeric_limits<s32>::max)(), 0x4f000000U}}) {
        CAPTURE(input);
        CHECK(translation.Run(u32(input), FloatSource, FloatDestination) == expected);
    }
}

TEST_CASE("Ampere immediate unsigned conversions preserve rounding and predicates",
          "[video_core][ampere][fast_fusion_logged]") {
    for (const auto original : {ImmediateFloatUp, ImmediateFloatTrunc}) {
        const bool round_up = original.Bits(0, 12) == 0x906;
        const auto baseline = LowerToMaxwell(std::array{original, Exit});
        INFO(baseline.error);
        REQUIRE(baseline);
        REQUIRE(baseline.instructions.size() == 2);
        const u32 exact_float = round_up ? 0x42140000U : 0x43820000U;
        CHECK(baseline.instructions[0] ==
              (0x010000000007f000ULL | (u64{exact_float} << 20) | original.Bits(16, 8)));
        for (const auto values :
             {std::array{0U, 0x00000000U, 0x00000000U}, std::array{1U, 0x3f800000U, 0x3f800000U},
              std::array{32U, 0x42000000U, 0x42000000U},
              std::array{16777215U, 0x4b7fffffU, 0x4b7fffffU},
              std::array{16777216U, 0x4b800000U, 0x4b800000U},
              std::array{16777217U, 0x4b800001U, 0x4b800000U},
              std::array{16777219U, 0x4b800002U, 0x4b800001U},
              std::array{33554431U, 0x4c000000U, 0x4bffffffU},
              std::array{33554433U, 0x4c000001U, 0x4c000000U},
              std::array{0x7fffffffU, 0x4f000000U, 0x4effffffU},
              std::array{0x80000000U, 0x4f000000U, 0x4f000000U},
              std::array{0x80000001U, 0x4f000001U, 0x4f000000U},
              std::array{0xffffff00U, 0x4f7fffffU, 0x4f7fffffU},
              std::array{0xffffffffU, 0x4f800000U, 0x4f7fffffU}}) {
            const u32 input = values[0], expected = values[round_up ? 1 : 2];
            for (u32 guard : {3U, 7U, 11U}) {
                auto instruction = original;
                instruction.words[0] = (instruction.words[0] & ~0xf000U) | (guard << 12);
                instruction.words[1] = input;
                const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
                INFO(lowered.error);
                REQUIRE(lowered);
                REQUIRE(lowered.instructions.size() == 2);
                CHECK((lowered.instructions[0] >> 56) == 1);
                CHECK(((lowered.instructions[0] >> 16) & 15) == guard);
                for (const bool predicate : {false, true}) {
                    LoggedMachine machine;
                    machine.predicates[3] = predicate;
                    auto registers = machine.registers;
                    const auto predicates = machine.predicates;
                    if (machine.Predicate(guard))
                        registers[0][original.Bits(16, 8)] = expected;
                    machine.Run(lowered.instructions);
                    CHECK(machine.registers == registers);
                    CHECK(machine.predicates == predicates);
                }
            }
        }
        for (u32 bit : {24U, 31U, 64U, 72U, 74U, 75U, 76U, 78U, 79U, 81U, 84U, 85U, 91U, 96U, 104U,
                        126U, 127U}) {
            CAPTURE(bit);
            auto invalid = original;
            invalid.words[bit / 32] ^= 1U << (bit % 32);
            const auto modified = LowerToMaxwell(std::array{invalid, Exit});
            if (bit == 74 || bit == 78 || bit == 79) {
                REQUIRE(modified);
                REQUIRE(modified.instructions.size() == 3);
                CHECK(modified.instructions[0] ==
                      (0x010000000007f0faULL | (u64{original.words[1]} << 20)));
                CHECK(modified.instructions[1] ==
                      (0x5cb800000fa70a00ULL | original.Bits(16, 8) |
                       (u64{invalid.Bits(74, 1)} << 13) | (u64{invalid.Bits(78, 2)} << 39)));
                HalfIR translation{modified.instructions};
                REQUIRE_FALSE(translation.block.Instructions().empty());
                if (bit == 74) {
                    CHECK(translation.Run(0, 0, original.Bits(16, 8)) == exact_float);
                }
            } else {
                CHECK_FALSE(modified);
            }
        }
        auto scratch = original;
        scratch.words[0] = (scratch.words[0] & ~0xff0000U) | (250U << 16);
        CHECK_FALSE(LowerToMaxwell(std::array{scratch, Exit}));
        CHECK_FALSE(LowerToMaxwell(std::array{UniformBank, scratch, Exit}));
    }
}

TEST_CASE("Fast Fusion nonadjacent call links require a closed write-safe window and callee",
          "[video_core][ampere][fast_fusion_logged]") {
    const std::array<Instruction, 7> original{{
        {{0x00097802, 0x30, 0xf00, 0}},
        {{0x08087227, 3, 0x078e00ff, 0}},
        {{0x00007944, 0x10, 0x03c00000, 0}},
        Exit,
        ImmediateFloatTrunc,
        {{0x00047202, 9, 0xf00, 0}},
        {{0x04007950, 0xffffff90, 0x03c3ffff, 0}},
    }};
    const auto lowered = LowerToMaxwell(original);
    INFO(lowered.error);
    REQUIRE(lowered);
    CHECK(lowered.source_size == sizeof(original));
    CHECK(lowered.instructions.back() == 0xe32000000007000fULL);
    for (const auto clobber : {
             Instruction{{0x00097802, 0, 0xf00, 0}},
             Instruction{{0x00090802, 0, 0xf00, 0}},
             Instruction{{0x01087225, 2, 0x078e00ff, 0}},
             Instruction{{0x00087984, 0, 0xa00, 0}},
             Instruction{{0x00080984, 0, 0xc00, 0}},
             Instruction{{0x00087981, 0, 0x0c1e1d00, 0}},
             Instruction{{0x0008783b, 0, 0x100, 0}},
         }) {
        for (const u32 site : {1U, 4U}) {
            auto code = original;
            code[site] = clobber;
            const auto rejected = LowerToMaxwell(code);
            CHECK_FALSE(rejected);
            CHECK(rejected.error == "native call linkage mode");
        }
    }
    for (const u32 entry_opcode : {0x947U, 0x944U, 0x945U}) {
        std::vector<Instruction> code{original.begin(), original.end()};
        code.push_back(Instruction{{0x7000U | entry_opcode, 0xffffff90,
                                    entry_opcode == 0x944 ? 0x03c3ffffU : 0x0383ffffU, 0}});
        CHECK_FALSE(LowerToMaxwell(code));
    }
    for (const u32 site : {0U, 2U, 5U}) {
        auto code = original;
        code[site].words[0] &= ~0xf000U;
        CHECK_FALSE(LowerToMaxwell(code));
    }
    for (const u32 site : {1U, 4U}) {
        auto unknown = original;
        // FMNMX with a write width outside the link proof's understood opcodes.
        unknown[site] = FixtureInstruction(0x209, 21, 26, 30, SourceTrue);
        const auto rejected = LowerToMaxwell(unknown);
        CHECK_FALSE(rejected);
        CHECK(rejected.error == "native call linkage mode");
    }
    auto wrong_copy = original;
    wrong_copy[5].words[1] = 8;
    CHECK_FALSE(LowerToMaxwell(wrong_copy));
    auto wrong_return = original;
    wrong_return[6].words[1] = 0xffffffa0;
    CHECK_FALSE(LowerToMaxwell(wrong_return));
    std::vector<Instruction> callee_entry{original.begin(), original.end()};
    callee_entry.push_back(Instruction{{0x00007945, 0xffffffd0, 0x0383ffff, 0}});
    CHECK_FALSE(LowerToMaxwell(callee_entry));
}

TEST_CASE("Fast Fusion adjacent callees reject implicit vector writes over their links",
          "[video_core][ampere][fast_fusion_logged]") {
    for (const auto load :
         {Instruction{{0x00087984, 0, 0xa00, 0}}, Instruction{{0x00087984, 0, 0xc00, 0}},
          Instruction{{0x00080984, 0, 0xc00, 0}}, Instruction{{0x00087981, 0, 0x0c1e1d00, 0}},
          Instruction{{0x0008783b, 0, 0x100, 0}}, Instruction{{0x0008783b, 0, 0x200, 0}}}) {
        const std::array<Instruction, 5> code{{
            {{0x00097802, 0x20, 0xf00, 0}},
            {{0x00007944, 0x10, 0x03c00000, 0}},
            Exit,
            load,
            {{0x09007950, 0xffffffb0, 0x03c3ffff, 0}},
        }};
        const auto lowered = LowerToMaxwell(code);
        CHECK_FALSE(lowered);
        CHECK(lowered.error == "native callee overwrites link register");
    }
}

TEST_CASE("Fast Fusion bounded callees reject branch and fallthrough entry",
          "[video_core][ampere][fast_fusion_logged]") {
    const std::array<Instruction, 8> branch_entry{{
        {{0x00000947, 0x40, 0x03800000, 0}},
        {{0x00097802, 0x40, 0xf00, 0}},
        {{0x08087227, 3, 0x078e00ff, 0}},
        {{0x00007944, 0x10, 0x03c00000, 0}},
        Exit,
        ImmediateFloatTrunc,
        {{0x00047202, 9, 0xf00, 0}},
        {{0x04007950, 0xffffff80, 0x03c3ffff, 0}},
    }};
    const std::array<Instruction, 9> fallthrough_entry{{
        {{0x00000947, 0x40, 0x03800000, 0}},
        {{0x00097802, 0x40, 0xf00, 0}},
        {{0x08087227, 3, 0x078e00ff, 0}},
        {{0x00007944, 0x20, 0x03c00000, 0}},
        Exit,
        {{0x00007918, 0, 0, 0}},
        ImmediateFloatTrunc,
        {{0x00047202, 9, 0xf00, 0}},
        {{0x04007950, 0xffffff70, 0x03c3ffff, 0}},
    }};
    const auto branch = LowerToMaxwell(branch_entry);
    CHECK_FALSE(branch);
    CHECK(branch.error == "native call linkage mode");
    const auto fallthrough = LowerToMaxwell(fallthrough_entry);
    CHECK_FALSE(fallthrough);
    CHECK(fallthrough.error == "native call linkage mode");
    auto closed = branch_entry;
    closed[0].words[1] = 0x30;
    const auto accepted = LowerToMaxwell(closed);
    INFO(accepted.error);
    CHECK(accepted);
}

TEST_CASE("Fast Fusion dead call intervals preserve source PCs and never accept reachable WARPSYNC",
          "[video_core][ampere][fast_fusion_logged]") {
    const Instruction warp_sync{{0x00007348, 7, 0x03800000, 0}};
    const std::array<Instruction, 13> original{{
        {{0x00097802, 0x30, 0xf00, 0}},
        {{0x08087227, 3, 0x078e00ff, 0}},
        {{0x00007944, 0x50, 0x03c00000, 0}},
        {{0x06007947, 0x33, 0x0b800000, 0}},
        {{0x00047802, 0x60, 0xf00, 0}},
        {{0x00007944, 0x50, 0x03c00000, 0}},
        {{0x00007918, 0, 0, 0}},
        Exit,
        ImmediateFloatTrunc,
        {{0x00047202, 9, 0xf00, 0}},
        {{0x04007950, 0xffffff50, 0x03c3ffff, 0}},
        warp_sync,
        {{0x04007950, 0xffffff30, 0x03c3ffff, 0}},
    }};
    const auto lowered = LowerToMaxwell(original);
    INFO(lowered.error);
    REQUIRE(lowered);
    CHECK(lowered.source_size == 0xb0);
    REQUIRE(lowered.source_instruction_offsets.size() == 11);
    for (u32 index = 4; index < 7; ++index)
        CHECK(lowered.source_instruction_offsets[index] == lowered.source_instruction_offsets[7]);
    u32 calls = 0;
    for (u32 index = 0; index < lowered.instructions.size(); ++index) {
        const u64 word = lowered.instructions[index];
        calls += (word & 0xfff0000000000000ULL) == 0xe260000000000000ULL;
        if ((word & 0xfff0000000000000ULL) == 0xe260000000000000ULL ||
            (word & 0xfff0000000000000ULL) == 0xe240000000000000ULL) {
            const s32 displacement = static_cast<s32>(u32(word >> 20) << 8) >> 8;
            const u32 source_target = (word >> 52) == 0xe26 ? 8U : 7U;
            CHECK(s64{Address(index)} + 8 + displacement ==
                  Address(lowered.source_instruction_offsets[source_target]));
        }
    }
    CHECK(calls == 1);
    auto guarded = original;
    guarded[3].words[0] &= ~0xf000U;
    CHECK_FALSE(LowerToMaxwell(guarded));
    for (const u32 opcode : {0x947U, 0x945U, 0x949U}) {
        std::vector<Instruction> code{original.begin(), original.end()};
        code.push_back(Instruction{{0x7000U | opcode, 0xffffff60, 0x0383ffff, 0}});
        CHECK_FALSE(LowerToMaxwell(code));
    }
    const auto reachable = LowerToMaxwell(std::array{warp_sync, Exit});
    CHECK_FALSE(reachable);
    CHECK(reachable.error == "unsupported native opcode");
}

TEST_CASE("Ampere register-lane shuffle preserves modes, masks and predicates",
          "[video_core][ampere][fast_fusion_logged]") {
    const auto baseline = LowerToMaxwell(std::array{ShuffleDown, Exit});
    INFO(baseline.error);
    REQUIRE(baseline);
    REQUIRE(baseline.instructions.size() == 2);
    CHECK(baseline.instructions[0] ==
          (0xef100000a0070000ULL | (u64{ShufflePredicate} << 48) | (u64{ShuffleMask} << 34) |
           (u64{ShuffleLane} << 20) | (u64{ShuffleSource} << 8) | ShuffleDestination));
    const std::array operations{IR::Opcode::ShuffleIndex, IR::Opcode::ShuffleUp,
                                IR::Opcode::ShuffleDown, IR::Opcode::ShuffleButterfly};
    for (u32 mode = 0; mode < operations.size(); ++mode) {
        for (u32 guard : {3U, 7U, 11U}) {
            for (u32 mask : {0x1fU, 0x1c03U}) {
                auto instruction = ShuffleDown;
                instruction.words[0] = (instruction.words[0] & ~0xf000U) | (guard << 12);
                instruction.words[1] = ShuffleLane | (mask << 8) | (mode << 26);
                const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
                INFO(lowered.error);
                REQUIRE(lowered);
                REQUIRE(lowered.instructions.size() == 2);
                const u64 word = lowered.instructions[0];
                CHECK(((word >> 16) & 15) == guard);
                CHECK(((word >> 28) & 3) == 2);
                Shader::ObjectPool<IR::Inst> pool;
                IR::Block block{pool};
                HalfEnvironment env;
                Shader::Maxwell::TranslatorVisitor visitor{env, block};
                visitor.SHFL(word);
                const IR::Inst* shuffle = nullptr;
                u32 predicate_writes = 0, register_writes = 0;
                for (const auto& inst : block.Instructions()) {
                    if (inst.GetOpcode() == operations[mode]) {
                        shuffle = &inst;
                        for (u32 operand = 0; operand < 2; ++operand) {
                            REQUIRE(inst.Arg(operand).Inst()->GetOpcode() ==
                                    IR::Opcode::GetRegister);
                            CHECK(static_cast<u32>(inst.Arg(operand).Inst()->Arg(0).Reg()) ==
                                  (operand ? ShuffleLane : ShuffleSource));
                        }
                        for (u32 operand = 2; operand < 4; ++operand) {
                            const auto& extract = *inst.Arg(operand).Inst();
                            REQUIRE(extract.GetOpcode() == IR::Opcode::BitFieldUExtract);
                            CHECK(extract.Arg(0).U32() == mask);
                            CHECK(extract.Arg(1).U32() == (operand == 2 ? 0U : 8U));
                            CHECK(extract.Arg(2).U32() == 5);
                        }
                    } else if (inst.GetOpcode() == IR::Opcode::SetPred) {
                        ++predicate_writes;
                        CHECK(inst.Arg(0).Pred() == static_cast<IR::Pred>(ShufflePredicate));
                        const auto& bounds = *inst.Arg(1).Inst();
                        REQUIRE(bounds.GetOpcode() == IR::Opcode::GetInBoundsFromOp);
                        CHECK(bounds.Arg(0).Inst() == shuffle);
                    } else if (inst.GetOpcode() == IR::Opcode::SetRegister) {
                        ++register_writes;
                        CHECK(inst.Arg(0).Reg() == static_cast<IR::Reg>(ShuffleDestination));
                        CHECK(inst.Arg(1).Inst() == shuffle);
                    }
                }
                REQUIRE(shuffle);
                CHECK(predicate_writes == 1);
                CHECK(register_writes == 1);
            }
        }
    }
    for (u32 bit : {53U, 54U, 55U, 56U, 57U, 60U, 61U, 62U, 63U, 64U, 72U, 80U, 84U, 91U, 96U, 104U,
                    126U, 127U}) {
        auto invalid = ShuffleDown;
        invalid.words[bit / 32] |= 1U << (bit % 32);
        CHECK_FALSE(LowerToMaxwell(std::array{invalid, Exit}));
    }
    for (u32 bit : {16U, 24U, 32U}) {
        auto scratch = ShuffleDown;
        scratch.words[bit / 32] =
            (scratch.words[bit / 32] & ~(255U << (bit % 32))) | (250U << (bit % 32));
        CHECK_FALSE(LowerToMaxwell(std::array{scratch, Exit}));
        CHECK_FALSE(LowerToMaxwell(std::array{UniformBank, scratch, Exit}));
    }
    auto mirrored_lane = ShuffleDown;
    mirrored_lane.words[1] = (mirrored_lane.words[1] & ~255U) | 224U;
    CHECK_FALSE(LowerToMaxwell(std::array{UniformBank, mirrored_lane, Exit}));
    for (u32 bit : {16U, 24U, 32U}) {
        auto zero_register = ShuffleDown;
        zero_register.words[bit / 32] |= 255U << (bit % 32);
        CHECK(LowerToMaxwell(std::array{zero_register, Exit}));
    }
}

TEST_CASE("Ampere immediate select shadows only its register operands",
          "[video_core][ampere][fast_fusion_logged]") {
    const auto lowered = LowerToMaxwell(std::array{UniformBank, ImmediateSelect, Exit});
    INFO(lowered.error);
    REQUIRE(lowered);
    for (const bool condition : {false, true}) {
        LoggedMachine machine;
        machine.predicates[0] = condition;
        const auto before = machine.registers;
        const auto predicates = machine.predicates;
        machine.Run(lowered.instructions);
        for (u32 reg = 0; reg < 255; ++reg) {
            if (reg < 246)
                CHECK(machine.registers[0][reg] ==
                      (reg == BankSeedMirror ? machine.Constant(BankSeedOffset, 4)
                                             : before[0][reg]));
            CHECK(machine.registers[1][reg] ==
                  (reg == ImmediateSelectShadow ? (condition ? SelectImmediate : 0U)
                                                : before[1][reg]));
        }
        CHECK(machine.predicates == predicates);
    }
}

TEST_CASE(
    "Ampere extended uniform adds consume both UP inputs without changing predicates",
    "[video_core][ampere][fast_fusion_logged]") {
    for (const auto instruction : {UniformExtendedCarry, UniformExtendedTwoCarries}) {
        DYNAMIC_SECTION("mode " << instruction.words[2]) {
            const auto lowered = LowerToMaxwell(std::array{instruction, Exit});
            INFO(lowered.error);
            REQUIRE(lowered);
            const u32 destination = UniformMirror + instruction.Bits(16, 8);
            const u32 source_a = UniformMirror + instruction.Bits(24, 8);
            const u32 source_b = UniformMirror + instruction.Bits(32, 8);
            const u32 source_c = instruction.Bits(64, 8);
            for (u32 packed = 0; packed < 128; ++packed) {
                for (u32 a : {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
                    for (u32 b : {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
                        LoggedMachine machine;
                        machine.registers[0][source_a] = a;
                        machine.registers[0][source_b] = b;
                        machine.registers[0][UniformPredicates] = packed | 0xface0000U;
                        const auto before = machine.registers[0];
                        const auto predicates = machine.predicates;
                        u32 expected =
                            a + b + (source_c == ZeroUniform ? 0U
                                                            : before[UniformMirror + source_c]);
                        for (u32 bit : {77U, 87U}) {
                            const u32 input = instruction.Bits(bit, 4), index = input & 7;
                            expected += (index == 7 || bool(packed & (1U << (6 - index)))) !=
                                        bool(input & 8);
                        }
                        machine.Run(lowered.instructions);
                        CAPTURE(a, b, packed);
                        for (u32 reg = 0; reg < 246; ++reg)
                            CHECK(machine.registers[0][reg] ==
                                  (reg == destination ? expected : before[reg]));
                        CHECK(machine.predicates == predicates);
                    }
                }
            }
        }
    }
}

TEST_CASE("Ampere opcode support keeps nearby unsupported modes rejected",
          "[video_core][ampere][fast_fusion_logged]") {
    const auto reject = [](Instruction instruction, u32 bit) {
        instruction.words[2] ^= 1U << bit;
        CHECK_FALSE(LowerToMaxwell(std::array{UniformBank, instruction, Exit}));
    };
    reject(ShadowMove, 12);
    reject(ShadowZero, 12);
    auto compare = ShadowCompare;
    compare.words[2] |= 0xc00U;
    CHECK_FALSE(LowerToMaxwell(std::array{UniformBank, compare, Exit}));
    auto load = ByteLoad;
    load.words[2] |= 0xe00U;
    CHECK_FALSE(LowerToMaxwell(std::array{load, Exit}));
    reject(ByteLoad, 27);
    reject(UniformMultiply, 10);
    reject(UniformMultiply, 17);
    reject(ShadowMin, 10);
    reject(ShadowPairZero, 16);
    reject(ShadowMultiply, 10);
    reject(HalfConversion, 27);
    reject(HalfConversion, 9);
    reject(HalfConversion, 12);
    reject(HalfConversion, 15);
    reject(HalfFloor, 9);
    reject(HalfFloor, 12);
    reject(SignedWordToHalf, 10);
    reject(SignedWordToHalf, 14);
    reject(FloatUp, 15);
    for (const auto instruction : {HalfTrunc, HalfRound}) {
        reject(instruction, 14);
        auto modified = instruction;
        modified.words[1] |= 1U << 29;
        CHECK_FALSE(LowerToMaxwell(std::array{modified, Exit}));
        modified = instruction;
        modified.words[3] |= 1U;
        CHECK_FALSE(LowerToMaxwell(std::array{modified, Exit}));
    }
    for (u32 bit : {8U, 14U, 15U, 27U})
        reject(UniformPack, bit);
    auto uniform_pack = UniformPack;
    uniform_pack.words[0] &= ~0xff000000U;
    CHECK_FALSE(LowerToMaxwell(std::array{uniform_pack, Exit}));
    uniform_pack = UniformPack;
    uniform_pack.words[1] = 255;
    CHECK_FALSE(LowerToMaxwell(std::array{uniform_pack, Exit}));
    for (u32 bit : {2U, 3U, 9U, 10U, 11U, 13U, 14U, 15U, 16U, 26U})
        reject(ScopedLoad, bit);
    auto scoped_load = ScopedLoad;
    scoped_load.words[1] |= 1U << 6;
    CHECK_FALSE(LowerToMaxwell(std::array{scoped_load, Exit}));
    scoped_load = ScopedLoad;
    scoped_load.words[1] = ScopedPolicyUniform + 1;
    CHECK_FALSE(LowerToMaxwell(std::array{scoped_load, Exit}));
    scoped_load = ScopedLoad;
    scoped_load.words[0] += 1U << 16;
    CHECK_FALSE(LowerToMaxwell(std::array{scoped_load, Exit}));
    for (const auto instruction : {HalfConversion, HalfFloor, SignedWordToHalf}) {
        auto modified = instruction;
        modified.words[0] |= 1U << 24;
        CHECK_FALSE(LowerToMaxwell(std::array{modified, Exit}));
        modified = instruction;
        modified.words[1] |= 1U << 29;
        CHECK_FALSE(LowerToMaxwell(std::array{modified, Exit}));
        modified = instruction;
        modified.words[3] |= 1U;
        CHECK_FALSE(LowerToMaxwell(std::array{modified, Exit}));
    }
    for (u32 bit : {8U, 10U, 11U})
        reject(UniformTwoCarries, bit);
    for (const auto instruction :
         {UniformCarry, UniformTwoCarries, UniformExtendedCarry, UniformExtendedTwoCarries}) {
        for (u32 bit : {8U, 9U, 11U, 12U})
            reject(instruction, bit);
        auto guarded = instruction;
        guarded.words[0] &= ~0xf000U;
        CHECK_FALSE(LowerToMaxwell(std::array{guarded, Exit}));
        auto modified = instruction;
        modified.words[1] |= 1U << 31;
        CHECK_FALSE(LowerToMaxwell(std::array{modified, Exit}));
    }
    auto duplicate_carry = UniformTwoCarries;
    duplicate_carry.words[2] =
        (duplicate_carry.words[2] & ~(7U << 17)) | (duplicate_carry.Bits(84, 3) << 17);
    CHECK_FALSE(LowerToMaxwell(std::array{duplicate_carry, Exit}));
    auto predicates = ShadowPredicates;
    predicates.words[1] |= 0x80U;
    CHECK_FALSE(LowerToMaxwell(std::array{UniformBank, predicates, Exit}));
    for (u32 bit : {6U, 8U, 9U, 11U, 12U, 13U, 17U, 18U, 27U})
        reject(AsyncCopy, bit);
    auto copy = AsyncCopy;
    copy.words[0] |= 1U << 24;
    CHECK_FALSE(LowerToMaxwell(std::array{copy, Exit}));
    copy = AsyncCopy;
    copy.words[0] = (copy.words[0] & ~0xff000000U) | (246U << 24);
    CHECK_FALSE(LowerToMaxwell(std::array{copy, Exit}));
    const Instruction masked_wait{{0x0000791a, 0x00008001, 0, 0}};
    const auto ordered = LowerToMaxwell(std::array{masked_wait, Exit});
    REQUIRE(ordered);
    REQUIRE(ordered.instructions.size() == 1);
    CHECK(ordered.instructions.front() == 0xe30000000007000fULL);
    const auto copying = LowerToMaxwell(std::array{AsyncCopy, masked_wait, Exit});
    REQUIRE(copying);
    CHECK(copying.instructions[copying.source_instruction_offsets[1]] == 0xef98000000070000ULL);
    CHECK(copying.source_instruction_offsets[2] == copying.source_instruction_offsets[1] + 1);
    auto invalid_wait = masked_wait;
    invalid_wait.words[1] |= 1U << 16;
    CHECK_FALSE(LowerToMaxwell(std::array{invalid_wait, Exit}));
    CHECK_FALSE(Shader::Ampere::LowerToMaxwellWith(std::array{UniformBank, ShadowMove, Exit}, 2,
                                                   false, false));
    for (u32 bit : {12U, 13U, 14U, 15U}) {
        copy = AsyncCopy;
        copy.words[1] ^= 1U << bit;
        CHECK_FALSE(LowerToMaxwell(std::array{copy, Exit}));
    }
    auto scratch = ShadowMove;
    scratch.words[0] = (scratch.words[0] & ~0xff0000U) | (246U << 16);
    CHECK_FALSE(LowerToMaxwell(std::array{scratch, Exit}));
}
