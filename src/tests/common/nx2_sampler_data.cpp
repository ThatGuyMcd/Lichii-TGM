// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <limits>
#include <sstream>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/nx2_sampler_data.h"

namespace {
struct Sample {
    unsigned thread{};
    unsigned depth{};
    bool faulted{};
    std::array<u64, 4> frames{};
};
using Nx2Sampler::Detail::RawModule;
using Nx2Sampler::Detail::RawThread;
using Nx2Sampler::Detail::WriteRawSnapshot;
} // namespace

TEST_CASE("Raw sampler output retains every PC beyond the text report cutoff",
          "[common][nx2_sampler]") {
    const std::array threads{RawThread{42, "GPUBack;\"quoted\""}, RawThread{77, "CPUCore_0"}};
    const std::array modules{RawModule{0x1234567800000000, 0x1234567800010000,
                                      "C:\\a path\\\"test\"\\lichii-tgm.exe"}};
    std::vector<Sample> samples;
    for (u64 pc = 0; pc < 100; ++pc) {
        samples.push_back({0, 2, false, {0x1234567800000000 + pc, 0x1234567800004321}});
    }
    // An empty/faulted walk is retained, rather than disappearing from the denominator.
    samples.push_back({1, 0, true, {}});
    std::ostringstream output;
    REQUIRE(WriteRawSnapshot<Sample>(output, "source-sha", threads, modules, samples, 4, 10, 10.1));
    const auto raw = nlohmann::json::parse(output.str());
    CHECK(raw.at("format") == "nx2-samples-raw");
    CHECK(raw.at("version") == 1);
    CHECK(raw.at("source_commit") == "source-sha");
    CHECK(raw.at("sample_count") == 101);
    CHECK(raw.at("samples").size() == 101);
    CHECK(raw.at("threads")[0].at("name") == threads[0].name);
    CHECK(raw.at("threads")[1].at("id") == 77);
    CHECK(raw.at("modules")[0].at("path") == modules[0].path);
    CHECK(raw.at("modules")[0].at("base") == "0x1234567800000000");
    for (size_t pc = 0; pc < 100; ++pc) {
        CHECK(raw.at("samples")[pc].at("frames")[0] ==
              fmt::format("{:#x}", 0x1234567800000000 + pc));
        CHECK(raw.at("samples")[pc].at("frames")[1] == "0x1234567800004321");
    }
    CHECK(raw.at("samples")[100].at("thread") == 1);
    CHECK(raw.at("samples")[100].at("faulted") == true);
    CHECK(raw.at("samples")[100].at("frames").empty());
}

TEST_CASE("Raw sampler output rejects invalid thread and depth metadata",
          "[common][nx2_sampler]") {
    const std::array threads{RawThread{42, "GPU"}};
    const std::array<RawModule, 0> modules{};
    std::array samples{Sample{1, 0, false, {}}};
    std::ostringstream output;
    CHECK_FALSE(WriteRawSnapshot<Sample>(output, "sha", threads, modules, samples, 4, 1, 1));
    CHECK(output.str().empty());
    samples[0] = {0, 5, false, {}};
    CHECK_FALSE(WriteRawSnapshot<Sample>(output, "sha", threads, modules, samples, 8, 1, 1));
    samples[0] = {0, 2, false, {}};
    CHECK_FALSE(WriteRawSnapshot<Sample>(output, "sha", threads, modules, samples, 1, 1, 1));
    CHECK(output.str().empty());
}

TEST_CASE("Raw sampler output reports stream failure and invalid timing",
          "[common][nx2_sampler]") {
    const std::array<RawThread, 0> threads{};
    const std::array<RawModule, 0> modules{};
    const std::array<Sample, 0> samples{};
    std::ostringstream output;
    CHECK_FALSE(WriteRawSnapshot<Sample>(output, "sha", threads, modules, samples, 4,
                                         std::numeric_limits<double>::infinity(), 1));
    CHECK(output.str().empty());
    output.setstate(std::ios::badbit);
    CHECK_FALSE(WriteRawSnapshot<Sample>(output, "sha", threads, modules, samples, 4, 1, 1));
}
