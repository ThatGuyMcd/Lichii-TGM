// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <catch2/catch_test_macros.hpp>

#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/memory_manager.h"
#include "video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"

namespace {
using Regs = Tegra::Engines::Maxwell3D::Regs;
using Topology = Regs::PrimitiveTopology;

TEST_CASE("Mesh draws rasterize SET_MESH_SHADER_A's topology", "[video_core][mesh_raster]") {
    const auto regs = std::make_unique<Regs>();
    REQUIRE(regs->RasterTopology(Topology::Points) == Topology::Points);
    REQUIRE(regs->RasterTopology(Topology::TriangleStrip) == Topology::TriangleStrip);

    regs->reg_array[Regs::MeshControl] = 1;
    regs->reg_array[Regs::MeshShaderA] = (64u << 13) | (64u << 4) | 4;
    REQUIRE(regs->RasterTopology(Topology::Points) == Topology::Triangles);
    regs->reg_array[Regs::MeshShaderA] = (64u << 4) | 1;
    REQUIRE(regs->RasterTopology(Topology::Points) == Topology::Lines);
    regs->reg_array[Regs::MeshShaderA] = 0;
    REQUIRE(regs->RasterTopology(Topology::Triangles) == Topology::Points);
}

TEST_CASE("Pipeline depth bias follows mesh output instead of draw points",
          "[video_core][mesh_raster]") {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    Tegra::MemoryManager memory{system, device_memory, 32, 1ULL << 31};
    const auto engine = std::make_unique<Tegra::Engines::Maxwell3D>(memory);
    auto& regs = engine->regs;
    engine->draw_manager.draw_state.topology = Topology::Points;
    regs.polygon_offset_point_enable = 0;
    regs.polygon_offset_line_enable = 0;
    regs.polygon_offset_fill_enable = 1;
    Vulkan::DynamicFeatures features{};
    Vulkan::FixedPipelineState state{};

    regs.reg_array[Regs::MeshControl] = 1;
    regs.reg_array[Regs::MeshShaderA] = (64u << 13) | (64u << 4) | 4;
    state.Refresh(*engine, features);
    CHECK(state.dynamic_state.depth_bias_enable.Value() == 1);
    regs.reg_array[Regs::MeshShaderA] = 1;
    state.Refresh(*engine, features);
    CHECK(state.dynamic_state.depth_bias_enable.Value() == 0);
    regs.polygon_offset_line_enable = 1;
    state.Refresh(*engine, features);
    CHECK(state.dynamic_state.depth_bias_enable.Value() == 1);
    regs.reg_array[Regs::MeshControl] = 0;
    state.Refresh(*engine, features);
    CHECK(state.dynamic_state.depth_bias_enable.Value() == 0);
}

TEST_CASE("Raster topology is invalidated with Vulkan state", "[video_core][mesh_raster]") {
    Vulkan::StateTracker state;
    CHECK(state.ChangeRasterTopology(Topology::Triangles));
    CHECK_FALSE(state.ChangeRasterTopology(Topology::Triangles));
    CHECK(state.ChangeRasterTopology(Topology::Lines));
    state.InvalidateState();
    CHECK(state.ChangeRasterTopology(Topology::Lines));
    CHECK_FALSE(state.ChangeRasterTopology(Topology::Lines));
    state.InvalidateCommandBufferState();
    CHECK(state.ChangeRasterTopology(Topology::Lines));
}

} // Anonymous namespace
