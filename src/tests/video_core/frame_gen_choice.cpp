// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <limits>

#include "video_core/frame_gen_choice.h"
#include "video_core/present_frame_budget.h"

namespace {

using Settings::FrameGenType;
using VideoCore::ActiveFrameGenType;
using VideoCore::BlockedDlssg;
using VideoCore::DlssgBlock;
using VideoCore::DlssgSession;

constexpr DlssgSession OCARINA{
    .streamline = true,
    .dlss = true,
    .program_id = VideoCore::OCARINA_PROGRAM_ID,
    .loaded = true,
};

} // Anonymous namespace

TEST_CASE("FIFO frame generation respects the current display without changing requested counts",
          "[video_core][frame_gen_refresh]") {
    VideoCommon::PresentFrameBudget budget;
    for (size_t frame = 0; frame < 120; ++frame) {
        REQUIRE(budget.Limit(3, 1.0 / 60.0, 60.0) == 0);
    }
    for (size_t frame = 0; frame < 30; ++frame) {
        REQUIRE(budget.Limit(3, 1.0 / 30.0, 60.0) == 1);
    }
    budget.Reset();
    size_t generated = 0;
    for (size_t frame = 0; frame < 50; ++frame) {
        generated += budget.Limit(3, 1.0 / 50.0, 60.0);
    }
    REQUIRE(generated == 10);
    for (size_t frame = 0; frame < 30; ++frame) {
        REQUIRE(budget.Limit(1, 1.0 / 30.0, 144.0) == 1);
    }
}

TEST_CASE("FIFO frame budget discards stale credits across displays and stalls",
          "[video_core][frame_gen_refresh]") {
    VideoCommon::PresentFrameBudget budget;
    REQUIRE(budget.Limit(3, 1.0 / 50.0, 60.0) == 0);
    REQUIRE(budget.Limit(3, 1.0 / 50.0, 60.0) == 0);
    REQUIRE(budget.Limit(3, 1.0 / 50.0, 75.0) == 0);
    REQUIRE(budget.Limit(3, 1.0, 75.0) == 0);
    REQUIRE(budget.Limit(3, 1.0 / 50.0, 75.0) == 0);
    REQUIRE(budget.Limit(3, 0.0, 75.0) == 0);
    REQUIRE(budget.Limit(3, std::numeric_limits<double>::quiet_NaN(), 75.0) == 0);
    REQUIRE(budget.Limit(3, 1.0 / 50.0, 0.0) == 3);
    REQUIRE(budget.Limit(3, 1.0 / 50.0, std::numeric_limits<double>::quiet_NaN()) == 3);
    REQUIRE(budget.Limit(3, 1.0 / 50.0, 75.0) == 0);
}

TEST_CASE("DLSS Frame Generation runs only for a game with inputs, else FSR3", "[video_core]") {
    REQUIRE(BlockedDlssg(OCARINA) == DlssgBlock::None);
    REQUIRE(ActiveFrameGenType(FrameGenType::Dlssg, DlssgBlock::None) ==
            FrameGenType::Dlssg);

    DlssgSession fire_emblem = OCARINA;
    fire_emblem.program_id = 0x04007EF01F8AE000ULL;
    fire_emblem.loaded = false;
    REQUIRE(BlockedDlssg(fire_emblem) == DlssgBlock::NoGameInputs);

    DlssgSession no_dlss = OCARINA;
    no_dlss.dlss = false;
    REQUIRE(BlockedDlssg(no_dlss) == DlssgBlock::DlssOff);

    DlssgSession not_loaded = OCARINA;
    not_loaded.loaded = false;
    REQUIRE(BlockedDlssg(not_loaded) == DlssgBlock::NotLoaded);

    DlssgSession linux_build = OCARINA;
    linux_build.streamline = false;
    REQUIRE(BlockedDlssg(linux_build) == DlssgBlock::NoStreamline);

    for (const DlssgBlock block : {DlssgBlock::NoStreamline, DlssgBlock::DlssOff,
                                   DlssgBlock::NoGameInputs, DlssgBlock::NotLoaded}) {
        REQUIRE(ActiveFrameGenType(FrameGenType::Dlssg, block) == FrameGenType::Fsr3);
    }
    REQUIRE(ActiveFrameGenType(FrameGenType::Lsfg, DlssgBlock::NotLoaded) == FrameGenType::Lsfg);
    REQUIRE(ActiveFrameGenType(FrameGenType::Fsr3, DlssgBlock::None) == FrameGenType::Fsr3);
}
