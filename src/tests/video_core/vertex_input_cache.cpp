// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "video_core/vertex_input_cache.h"

// Ported from jpkto/Lichii e7581fa9f6 (src/tests/video_core/draw_reuse.cpp).

TEST_CASE("Vertex buffer bind reuse requires every field and the recording epoch",
          "[video_core][vertex_input_cache]") {
    VideoCommon::VertexBufferBindingCache<u64, 32> cache;
    CHECK_FALSE(cache.Matches(7, 10, 32, 128, 16, 4));
    // Comparison alone cannot publish a binding if the subsequent command enqueue fails.
    CHECK_FALSE(cache.Matches(7, 10, 32, 128, 16, 4));
    CHECK_FALSE(cache.Update(7, 10, 32, 128, 16, 4));
    CHECK(cache.Matches(7, 10, 32, 128, 16, 4));
    CHECK(cache.Update(7, 10, 32, 128, 16, 4));
    CHECK_FALSE(cache.Update(7, 11, 32, 128, 16, 4));
    CHECK_FALSE(cache.Update(7, 11, 64, 128, 16, 4));
    CHECK_FALSE(cache.Update(7, 11, 64, 256, 16, 4));
    CHECK_FALSE(cache.Update(7, 11, 64, 256, 32, 4));
    CHECK(cache.Update(7, 11, 64, 256, 32, 4));
    CHECK_FALSE(cache.Update(7, 11, 64, 256, 32, 5));
    CHECK(cache.Update(7, 11, 64, 256, 32, 5));
    CHECK_FALSE(cache.Update(8, 11, 64, 256, 32, 5));
    cache.Invalidate(7);
    CHECK_FALSE(cache.Update(7, 11, 64, 256, 32, 5));
    CHECK(cache.Update(8, 11, 64, 256, 32, 5));
    CHECK(cache.Update(7, 11, 64, 256, 32, 5));
    // An initially null binding still needs its first command, including at epoch zero.
    CHECK_FALSE(cache.Update(0, 0, 0, 0, 0, 0));
    CHECK(cache.Update(0, 0, 0, 0, 0, 0));
    CHECK_FALSE(cache.Update(32, 11, 64, 256, 32, 5));
    cache.Invalidate(32);
    CHECK(cache.Update(7, 11, 64, 256, 32, 5));
}

TEST_CASE("Vertex input states are found by every register they are built from",
          "[video_core][vertex_input_cache]") {
    struct State {
        u32 built_from;
    };
    VideoCommon::VertexInputCache<State> cache;
    VideoCommon::VertexInputKey key{.num_attributes = 32, .num_bindings = 32};
    key.attributes[0] = 0x3e400000;
    key.strides[0] = 16;
    CHECK(cache.Find(key, key.Hash()) == nullptr);
    cache.Insert(key, key.Hash(), std::make_shared<const State>(State{1}));
    const auto found = cache.Find(key, key.Hash());
    REQUIRE(found != nullptr);
    CHECK(found->built_from == 1);

    const auto differs = [&](auto&& change) {
        VideoCommon::VertexInputKey other = key;
        change(other);
        return !(other == key) && cache.Find(other, other.Hash()) == nullptr;
    };
    CHECK(differs([](auto& k) { k.attributes[31] = 1; }));
    CHECK(differs([](auto& k) { k.strides[5] = 4; }));
    CHECK(differs([](auto& k) { k.divisors[0] = 2; }));
    CHECK(differs([](auto& k) { k.instanced = 1U << 7; }));
    CHECK(differs([](auto& k) { k.repacked = 1U; }));
    CHECK(differs([](auto& k) { k.num_attributes = 16; }));
    CHECK(differs([](auto& k) { k.num_bindings = 16; }));

    SECTION("a replaced entry stays alive for the commands that recorded it") {
        // Colliding keys: same slot, different registers.
        VideoCommon::VertexInputKey other = key;
        for (u32 value = 1;; ++value) {
            other.attributes[1] = value;
            if (other.Hash() % VideoCommon::VertexInputCache<State>::NumEntries ==
                key.Hash() % VideoCommon::VertexInputCache<State>::NumEntries) {
                break;
            }
        }
        cache.Insert(other, other.Hash(), std::make_shared<const State>(State{2}));
        CHECK(cache.Find(key, key.Hash()) == nullptr);
        CHECK(found->built_from == 1);
        CHECK(cache.Find(other, other.Hash())->built_from == 2);
    }
}
