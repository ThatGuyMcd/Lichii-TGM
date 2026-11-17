// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include "video_core/oot_network.h"

TEST_CASE("OOT tonemap source requires a recognized draw in the current mapping epoch",
          "[video_core][oot_network][oot_tonemap_epoch]") {
    using namespace VideoCommon::OotNetwork;
    BypassState state;
    int channel_a{}, channel_b{};
    const BypassState::Tonemap image{0x1234000, 1920, 1080, 0, 4, 0};
    for (u64 hash : {0xbded5ce917183214ULL, 0x6cb06edf14be1d79ULL, 0x3ac011bd2d772994ULL}) {
        state.NoteDraw(hash, image, 5, &channel_a, 10);
        state.ValidateEpoch(5, &channel_a, 10);
        REQUIRE(state.HasTonemap());
        const auto consumed = state.TakeTonemap();
        REQUIRE(consumed);
        REQUIRE(consumed->address == image.address);
        REQUIRE(consumed->block_height == image.block_height);
        REQUIRE_FALSE(state.TakeTonemap());
    }
    state.NoteDraw(0xbded5ce917183214ULL, image, 5, &channel_a, 10);
    state.NoteDraw(0x1234, image, 5, &channel_a, 10);
    REQUIRE_FALSE(state.HasTonemap());
    // Clears and DrawTexture do not run PrepareDraw, so they explicitly break the source proof.
    state.NoteDraw(0xbded5ce917183214ULL, image, 5, &channel_a, 10);
    state.output_replaced.store(true);
    state.InvalidateTonemap();
    REQUIRE_FALSE(state.HasTonemap());
    REQUIRE_FALSE(state.TakeTonemap());
    REQUIRE(state.output_replaced.load());
    state.NoteDraw(0xbded5ce917183214ULL, image, 5, &channel_a, 10);
    state.ValidateEpoch(6, &channel_a, 10);
    REQUIRE_FALSE(state.HasTonemap());
    state.NoteDraw(0xbded5ce917183214ULL, image, 5, &channel_a, 10);
    state.ValidateEpoch(5, &channel_b, 10);
    REQUIRE_FALSE(state.HasTonemap());
    state.NoteDraw(0xbded5ce917183214ULL, image, 5, &channel_a, 10);
    state.ValidateEpoch(5, &channel_a, 11);
    REQUIRE_FALSE(state.HasTonemap());
    // A recognized shader without a compatible color target cannot establish a source either.
    state.NoteDraw(0xbded5ce917183214ULL, {}, 5, &channel_a, 10);
    REQUIRE_FALSE(state.HasTonemap());
}
