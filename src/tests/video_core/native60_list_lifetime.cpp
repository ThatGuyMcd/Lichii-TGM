// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "video_core/macro_turing.h"
#include "video_core/nx2_list_indirect.h"
namespace {
struct Mme {
    bool end{};
    u32 pred_mode{};
    u32 pred_reg{};
    std::array<u32, 2> op{}, dst{24, 24}, src_a{24, 24}, src_b{24, 24}, imm{};
    std::array<u32, 2> out_method{}, out_emit{};
};

std::vector<u32> Code(std::span<const Mme> instructions) {
    std::vector<u32> code;
    for (const Mme& f : instructions) {
        std::array<u32, 3> bits{};
        const auto field = [&bits](u32 offset, u32 count, u32 value) {
            for (u32 bit = 0; bit < count; ++bit) {
                bits[(offset + bit) / 32] |= ((value >> bit) & 1) << ((offset + bit) % 32);
            }
        };
        field(0, 1, f.end ? 1 : 0);
        field(1, 4, f.pred_mode);
        field(5, 5, f.pred_reg);
        for (u32 lane = 0; lane < 2; ++lane) {
            field(10 + lane * 36, 5, f.op[lane]);
            field(15 + lane * 36, 5, f.dst[lane]);
            field(20 + lane * 36, 5, f.src_a[lane]);
            field(25 + lane * 36, 5, f.src_b[lane]);
            field(30 + lane * 36, 16, f.imm[lane]);
            field(82 + lane * 7, 3, f.out_method[lane]);
            field(85 + lane * 7, 4, f.out_emit[lane]);
        }
        code.insert(code.end(), {bits[2], bits[1], bits[0]});
    }
    return code;
}


} // namespace
namespace {

namespace List = Tegra::Nx2ListIndirect;

/// A compute macro call as List::ComputeFront sees it (kepler_compute.cpp's ListCallContext), on
/// an engine whose registers take the call's emits; `released` gets the slots the front gives
/// back to the back.
struct ListCall {
    std::span<const u32> code;
    u64 generation;
    u32 start;
    std::vector<u32>& params;
    std::span<u32> data_ram;
    std::span<const u32> registers;
    List::UploadState upload;
    std::span<u32> engine_registers;
    std::vector<u32>& released;
    bool fail_emit{};

    [[nodiscard]] std::optional<u64> DeviceWord(u64) const {
        return std::nullopt;
    }
    void Emit(u32 method, u32 value) const {
        if (fail_emit) {
            throw std::runtime_error{"injected emit failure"};
        }
        engine_registers[method] = value;
    }
    void SendOwed(u64, u32) const {}
    void Release(u32 slot) const {
        released.push_back(slot);
    }
    void ReadSlots(std::span<const u32>, std::span<u32> words) const {
        std::ranges::fill(words, 7U);
    }
    void SetRegister(u32 method, u32 value) const {
        engine_registers[method] = value;
    }
};

} // Anonymous namespace

TEST_CASE("Native60: List words: a run that moves a slot's last holder keeps the slot held",
          "[video_core][macro]") {
    constexpr u32 slot = 5;
    constexpr u32 a = 0x10;
    constexpr u32 b = 0x20;
    const u32 word = List::Placeholder(slot, 0);
    std::array<u32, 0xc00> data_ram{};
    std::array<u32, 0x1000> registers{};
    std::vector<u32> params;
    std::vector<u32> released;
    List::DataRamWords channel{data_ram.data(), {}, {}};
    List::ComputeFront front;
    bool concretized{};
    List::Reason reason{};
    const auto call = [&](const std::vector<u32>& code, u64 generation) {
        return ListCall{code, generation, 0, params, data_ram, registers, {}, registers, released};
    };
    // DREAD of data RAM word `address` into r0; DWRITE of r0 (`source` 0), the parameter (28)
    // or 0 (24) to one.
    const auto dread = [](u32 address) {
        return Mme{.op = {30, 0}, .dst = {0, 24}, .src_a = {25, 24}, .imm = {address, 0}};
    };
    const auto dwrite = [](u32 address, u32 source, bool end) {
        return Mme{.end = end,
                   .op = {31, 0},
                   .src_a = {25, 24},
                   .src_b = {source, 24},
                   .imm = {address, 0}};
    };
    // A call whose parameter is the list's word stores it at A.
    const auto store = Code(std::vector<Mme>{dwrite(a, 28, true), Mme{}});
    auto stores = call(store, 1);
    REQUIRE(front.PrepareCall(stores, channel, slot, 1));
    params = {word};
    REQUIRE(front.Run(stores, channel, concretized, reason));
    REQUIRE(data_ram[a] == word);
    REQUIRE(List::FrontHolds()[slot] == 1);

    // Loads A, clears it, stores the word at B: the clear drops A's hold before B takes one.
    const auto move =
        Code(std::vector<Mme>{dread(a), dwrite(a, 24, false), dwrite(b, 0, true), Mme{}});
    params.clear();
    auto moves = call(move, 2);
    REQUIRE(front.Run(moves, channel, concretized, reason));
    REQUIRE(data_ram[a] == 0);
    REQUIRE(data_ram[b] == word);
    CHECK(released.empty());
    CHECK(List::FrontHolds()[slot] == 1);

    // Loads B, emits the word as a QMD grid word, then clears B: the run's stores are applied
    // before its emits.
    const Mme emit_r0{.src_a = {0, 24},
                      .imm = {List::InlineQmdGridFirst, 0},
                      .out_method = {5, 0},
                      .out_emit = {1, 0}};
    const auto consume = Code(std::vector<Mme>{dread(b), emit_r0, dwrite(b, 24, true), Mme{}});
    auto consumes = call(consume, 3);
    SECTION("emission throws after the last data RAM holder is cleared") {
        consumes.fail_emit = true;
        CHECK_THROWS_AS(front.Run(consumes, channel, concretized, reason), std::runtime_error);
        CHECK(data_ram[b] == 0);
        CHECK(released == std::vector<u32>{slot});
        CHECK(List::FrontHolds()[slot] == 0);
        return;
    }
    SECTION("emission transfers the hold into the QMD") {}
    REQUIRE(front.Run(consumes, channel, concretized, reason));
    REQUIRE(data_ram[b] == 0);
    REQUIRE(registers[List::InlineQmdGridFirst] == word);
    CHECK(released.empty());
    CHECK(List::FrontHolds()[slot] == 1);

    // The words read back: the front holds the slot no more and gives it back, once.
    front.ConcretizeAll(consumes, channel);
    CHECK(registers[List::InlineQmdGridFirst] == 7U);
    CHECK(released == std::vector<u32>{slot});
    CHECK(List::FrontHolds()[slot] == 0);
}

TEST_CASE("Native60: List words: a placeholder names a slot the back has", "[video_core][macro]") {
    CHECK(List::IsPlaceholder(List::Placeholder(0, 0)));
    CHECK(List::IsPlaceholder(List::Placeholder(List::SlotCount - 1, 2)));
    CHECK(List::GridSlot(List::Placeholder(9, 0), List::Placeholder(9, 1),
                         List::Placeholder(9, 2)) == 9U);
    // A grid the guest wrote with the magic and a slot past the last.
    const u32 past = List::Placeholder(List::SlotCount, 0);
    CHECK_FALSE(List::IsPlaceholder(past));
    CHECK_FALSE(List::IsPlaceholder(List::PlaceholderMagic | 0xFFFCU));
    CHECK_FALSE(List::GridSlot(past, past + 1, past + 2).has_value());
}
