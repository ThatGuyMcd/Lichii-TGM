// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/vulkan_common/graphics_pipeline_library_policy.h"

TEST_CASE("Graphics library layouts require five bound descriptor sets", "[video_core][gpl]") {
    using Vulkan::CanUseGraphicsPipelineLibraries;
    CHECK_FALSE(CanUseGraphicsPipelineLibraries(true, true, 4, true));
    CHECK(CanUseGraphicsPipelineLibraries(true, true, 5, true));
    CHECK(CanUseGraphicsPipelineLibraries(true, true, 32, true));
}

TEST_CASE("Independent graphics libraries require compatible linking properties",
          "[video_core][gpl]") {
    using Vulkan::CanUseGraphicsPipelineLibraries;
    CHECK_FALSE(CanUseGraphicsPipelineLibraries(false, true, 32, true));
    CHECK_FALSE(CanUseGraphicsPipelineLibraries(true, false, 32, true));
    CHECK_FALSE(CanUseGraphicsPipelineLibraries(true, true, 32, false));
    CHECK(CanUseGraphicsPipelineLibraries(true, true, 32, true));
}
