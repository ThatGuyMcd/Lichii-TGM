// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/program_header.h"
#include "video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "video_core/renderer_vulkan/native_key_compatibility.h"
#include "video_core/shader_cache.h"

namespace {
Vulkan::FixedPipelineState MakeState() {
    Vulkan::FixedPipelineState state;
    std::memset(&state, 0, sizeof(state));
    state.dynamic_vertex_input.Assign(1);
    // Float attributes 0-3, an unsigned one at 6.
    state.attribute_types = 0b01'01'01'01 | (u64{3} << 12);
    state.color_formats = {0xd5, 0xd5, 0, 0, 0, 0, 0, 0};
    for (auto& attachment : state.attachments) {
        attachment.raw = 0b1001 | (4U << 10) | (5U << 15) | (1U << 4);
    }
    state.attachments[0].raw |= 1U << 30;
    state.depth_bounds_min = 0x3f000000;
    state.depth_bounds_max = 0x3f800000;
    state.line_stipple_factor = 2;
    state.line_stipple_pattern = 0xf0f0;
    return state;
}
} // Anonymous namespace

TEST_CASE("Recorded native logic operations normalize only the known disabled case",
          "[canonical_native_key]") {
    Vulkan::DynamicFeatures features{};
    for (const bool dynamic_operation : {false, true}) {
        for (const bool dynamic_enable : {false, true}) {
            for (const bool enabled : {false, true}) {
                features.has_extended_dynamic_state_2_logic_op = dynamic_operation;
                features.has_dynamic_state3_logic_op_enable = dynamic_enable;
                auto recorded = MakeState();
                recorded.extended_dynamic_state_2_logic_op.Assign(1);
                recorded.dynamic_state.logic_op_enable.Assign(enabled);
                recorded.dynamic_state.logic_op.Assign(7);
                recorded.extended_dynamic_state_3_blend.Assign(1);
                const auto before = recorded;
                Vulkan::NormalizeRecordedNativeLogicOp(recorded, features);
                const bool known_disabled = !dynamic_enable && !enabled;
                CHECK(recorded.extended_dynamic_state_2_logic_op ==
                      (!dynamic_operation && known_disabled ? 0 : 1));
                CHECK(recorded.dynamic_state.logic_op == (known_disabled ? 0 : 7));
                // Unrelated dynamic-state mismatches must still be rejected by device checks.
                CHECK(recorded.extended_dynamic_state_3_blend ==
                      before.extended_dynamic_state_3_blend);
                CHECK(recorded.attribute_types == before.attribute_types);
                CHECK(recorded.dynamic_state.logic_op_enable == enabled);
            }
        }
    }
}

TEST_CASE("A converted recipe key matches a native disabled-logic-op draw",
          "[canonical_native_key]") {
    Vulkan::DynamicFeatures features{};
    auto live = MakeState();
    live.dynamic_state.logic_op.Assign(3);
    live.extended_dynamic_state_2_logic_op.Assign(0);
    auto recorded = live;
    recorded.dynamic_state.logic_op.Assign(7);
    recorded.extended_dynamic_state_2_logic_op.Assign(1);
    live.CanonicalizeNative(~0U);
    recorded.CanonicalizeNative(~0U);
    Vulkan::CanonicalizeNativeLogicOp(live, features);
    Vulkan::NormalizeRecordedNativeLogicOp(recorded, features);
    CHECK(recorded == live);
    CHECK(recorded.Hash() == live.Hash());
}

TEST_CASE("Canonical native keys keep the attribute types the vertex program reads",
          "[canonical_native_key]") {
    auto state = MakeState();
    state.CanonicalizeNative(0b0000'0101); // attributes 0 and 2
    CHECK(state.attribute_types == 0b01'00'01);

    auto every = MakeState();
    every.CanonicalizeNative(~0U);
    CHECK(every.attribute_types == MakeState().attribute_types);

    auto mesh = MakeState();
    mesh.CanonicalizeNative(0);
    CHECK(mesh.attribute_types == 0);

    // Without dynamic vertex input the field holds the divisors, which stay.
    auto divisors = MakeState();
    divisors.dynamic_vertex_input.Assign(0);
    divisors.CanonicalizeNative(0);
    CHECK(divisors.enabled_divisors == MakeState().enabled_divisors);
}

TEST_CASE("Canonical native keys clear blending the pipeline does not use",
          "[canonical_native_key]") {
    auto state = MakeState();
    const auto blended = state.attachments[0].raw;
    state.CanonicalizeNative(~0U);
    // Blending on: unchanged.
    CHECK(state.attachments[0].raw == blended);
    // Blending off: only the colour write mask is left.
    CHECK(state.attachments[1].raw == 0b1001);
    // Past the last colour target: cleared.
    for (size_t index = 2; index < state.attachments.size(); ++index) {
        CHECK(state.attachments[index].raw == 0);
    }

    // With dynamic blending the attachments are not part of the key and stay.
    auto dynamic = MakeState();
    dynamic.extended_dynamic_state_3_blend.Assign(1);
    dynamic.CanonicalizeNative(~0U);
    CHECK(dynamic.attachments[5].raw == MakeState().attachments[5].raw);
}

TEST_CASE("Canonical native keys clear dynamic depth bounds and an unused stipple",
          "[canonical_native_key]") {
    auto state = MakeState();
    state.CanonicalizeNative(~0U);
    CHECK(state.depth_bounds_min == 0);
    CHECK(state.depth_bounds_max == 0);
    CHECK(state.line_stipple_factor == 0);
    CHECK(state.line_stipple_pattern == 0);

    auto stippled = MakeState();
    stippled.dynamic_state.line_stipple_enable.Assign(1);
    stippled.CanonicalizeNative(~0U);
    CHECK(stippled.line_stipple_factor == 2);
    CHECK(stippled.line_stipple_pattern == 0xf0f0);
}

TEST_CASE("Draws that differ only in cleared state share a canonical key",
          "[canonical_native_key]") {
    auto a = MakeState();
    auto b = MakeState();
    b.attribute_types |= u64{2} << 40;            // an attribute the program does not read
    b.attachments[1].raw |= 7U << 25;            // blending off on target 1
    b.attachments[4].raw |= 1U << 30;            // no target 4
    b.depth_bounds_max = 0;
    REQUIRE((a.attachments[1].raw & 0xF) == 0b1001);
    REQUIRE((b.attachments[1].raw & 0xF) == 0b1001);
    CHECK(a.Hash() != b.Hash());
    a.CanonicalizeNative(0b1111);
    b.CanonicalizeNative(0b1111);
    CHECK(a == b);
    CHECK(a.Hash() == b.Hash());
}

TEST_CASE("The generic read mask follows the header's input map", "[canonical_native_key]") {
    Shader::ProgramHeader sph{};
    sph.vtg.imap_generic_vector[0] = 0x0f;  // attribute 0, all components
    sph.vtg.imap_generic_vector[0] |= 0x10; // attribute 1, x only
    sph.vtg.imap_generic_vector[15] = 0x80; // attribute 31, w only
    CHECK(VideoCommon::GenericReadMask(sph) == ((1U << 0) | (1U << 1) | (1U << 31)));
    CHECK(VideoCommon::GenericReadMask(Shader::ProgramHeader{}) == 0);
}
