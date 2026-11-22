// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <barrier>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "common/uuid.h"
#include "video_core/renderer_vulkan/pipeline_build_trace.h"

namespace {
using Clock = std::chrono::steady_clock;

u64 Now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
        .count();
}

struct TraceFile {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("pipeline-trace-test-" + Common::UUID::MakeRandom().FormattedString());
    std::unique_ptr<std::FILE, decltype(&std::fclose)> file{Open(path), &std::fclose};
    Vulkan::PipelineBuildTraceWriter writer{file.get()};

    static std::FILE* Open(const std::filesystem::path& path) {
#ifdef _WIN32
        return _wfopen(path.c_str(), L"w+b");
#else
        return std::fopen(path.c_str(), "w+b");
#endif
    }

    ~TraceFile() {
        file.reset();
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    std::vector<std::string> Lines() {
        REQUIRE(file);
        REQUIRE(std::fseek(file.get(), 0, SEEK_SET) == 0);
        std::vector<std::string> lines;
        std::array<char, 1024> buffer{};
        while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), file.get())) {
            lines.emplace_back(buffer.data());
        }
        REQUIRE(std::ferror(file.get()) == 0);
        return lines;
    }
};

std::string Field(const std::string& line, const std::string& name) {
    const std::string prefix = " " + name + "=";
    const auto at = line.find(prefix);
    REQUIRE(at != std::string::npos);
    const auto begin = at + prefix.size();
    return line.substr(begin, line.find_first_of(" \r\n", begin) - begin);
}
} // namespace

TEST_CASE("Pipeline trace pairs nested identical keys and timestamps the driver interval",
          "[video_core][pipeline_build_trace]") {
    TraceFile trace;
    REQUIRE(trace.file);
    const std::array<u64, 2> stages{0x123, 0x456};
    const u64 before = Now();
    const auto result =
        Vulkan::TracePipelineBuild(trace.writer, "graphics", 0xabc, stages, true, [&] {
            return Vulkan::TracePipelineBuild(trace.writer, "graphics", 0xabc, stages, true,
                                              [] { return 17; });
        });
    const u64 after = Now();
    REQUIRE(result == 17);
    const auto lines = trace.Lines();
    REQUIRE(lines.size() == 4);
    REQUIRE(lines[0].starts_with("BEGIN graphics pipeline=0000000000000abc thread="));
    REQUIRE(lines[1].starts_with("BEGIN graphics "));
    REQUIRE(lines[2].starts_with("END graphics "));
    REQUIRE(lines[3].starts_with("END graphics "));
    REQUIRE(Field(lines[0], "call") != Field(lines[1], "call"));
    REQUIRE(Field(lines[0], "call") == Field(lines[3], "call"));
    REQUIRE(Field(lines[1], "call") == Field(lines[2], "call"));
    for (const auto& line : lines) {
        CHECK(line.find(" unoptimized s0:0000000000000123 s1:0000000000000456") !=
              std::string::npos);
        CHECK(Field(line, "session_ns") == Field(lines[0], "session_ns"));
        CHECK(std::stoull(Field(line, "ts_ns")) >= before);
        CHECK(std::stoull(Field(line, "ts_ns")) <= after);
    }
    for (const size_t end : {2U, 3U}) {
        const size_t begin = 3 - end;
        const u64 call_begin = std::stoull(Field(lines[end], "call_begin_ns"));
        const u64 call_end = std::stoull(Field(lines[end], "ts_ns"));
        CHECK(call_begin >= std::stoull(Field(lines[begin], "ts_ns")));
        CHECK(call_end >= call_begin);
        CHECK(std::abs(std::stod(Field(lines[end], "ms")) -
                       static_cast<double>(call_end - call_begin) / 1e6) <= 0.051);
        CHECK(Field(lines[end], "outcome") == "success");
    }
}

TEST_CASE("Pipeline trace ends failed calls without changing their exception",
          "[video_core][pipeline_build_trace]") {
    TraceFile trace;
    REQUIRE(trace.file);
    const std::array<u64, 1> stages{0x123};
    REQUIRE_THROWS_WITH(Vulkan::TracePipelineBuild(
                            trace.writer, "compute", 0xabc, stages, false,
                            [] { throw std::runtime_error("driver rejection"); }, "mesh"),
                        "driver rejection");
    const auto lines = trace.Lines();
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].starts_with("BEGIN compute "));
    CHECK(lines[1].starts_with("END compute "));
    CHECK(Field(lines[0], "call") == Field(lines[1], "call"));
    CHECK(Field(lines[0], "role") == "mesh");
    CHECK(Field(lines[1], "role") == "mesh");
    CHECK(Field(lines[1], "outcome") == "exception");
}

TEST_CASE("Pipeline trace keeps concurrent fallback roles and call pairs intact",
          "[video_core][pipeline_build_trace]") {
    TraceFile trace;
    REQUIRE(trace.file);
    std::barrier rendezvous{3};
    {
        std::vector<std::jthread> threads;
        for (const char* role : {"mesh", "task", "distribution"}) {
            threads.emplace_back([&, role] {
                const std::array<u64, 1> stages{0x123};
                Vulkan::TracePipelineBuild(
                    trace.writer, "compute", 0xabc, stages, false,
                    [&] { rendezvous.arrive_and_wait(); }, role);
            });
        }
    }
    const auto lines = trace.Lines();
    REQUIRE(lines.size() == 6);
    std::map<std::string, std::vector<std::string>> calls;
    for (const auto& line : lines) {
        calls[Field(line, "call")].push_back(line);
    }
    REQUIRE(calls.size() == 3);
    for (const auto& [id, pair] : calls) {
        CAPTURE(id);
        REQUIRE(pair.size() == 2);
        CHECK(pair[0].starts_with("BEGIN compute "));
        CHECK(pair[1].starts_with("END compute "));
        CHECK(Field(pair[0], "role") == Field(pair[1], "role"));
        CHECK(Field(pair[0], "thread") == Field(pair[1], "thread"));
    }
}

TEST_CASE("Disabled pipeline tracing preserves returns and exceptions",
          "[video_core][pipeline_build_trace]") {
    Vulkan::PipelineBuildTraceWriter disabled{nullptr};
    unsigned calls{};
    CHECK(Vulkan::TracePipelineBuild(disabled, "compute", 0, {}, false, [&] { return ++calls; }) ==
          1);
    const auto owned = Vulkan::TracePipelineBuild(disabled, "compute", 0, {}, false,
                                                  [] { return std::make_unique<int>(42); });
    REQUIRE(owned);
    CHECK(*owned == 42);
    REQUIRE_THROWS_WITH(Vulkan::TracePipelineBuild(disabled, "compute", 0, {}, false,
                                                   [&] {
                                                       ++calls;
                                                       throw std::runtime_error("unchanged");
                                                   }),
                        "unchanged");
    CHECK(calls == 2);
}

TEST_CASE("Module trace publishes final word count before entering the driver",
          "[video_core][pipeline_build_trace][module_build_trace]") {
    TraceFile trace;
    REQUIRE(trace.file);
    // A deferred job starts with no emitted words. The final module is larger than the
    // emitted input, as can happen when a module transform adds instructions.
    std::vector<u32> final_words;
    auto deferred = [&] {
        return Vulkan::TraceShaderModuleBuild(
            trace.writer, "graphics-module", 0xabc, final_words,
            [&](std::span<const u32> driver_words) {
                CHECK(driver_words.data() == final_words.data());
                CHECK(driver_words.size_bytes() == 28);
                CHECK(driver_words[6] == 0x12345678);
                // A separate reader observes BEGIN while the driver call is in progress.
                // This tests flushing as well as avoiding the pre-emission zero count.
                std::ifstream reader{trace.path};
                std::string begin;
                REQUIRE(std::getline(reader, begin));
                CHECK(begin.starts_with("BEGIN graphics-module "));
                CHECK(Field(begin, "spirv_words") == "7");
                CHECK(begin.find(" s0:0000000000000abc") != std::string::npos);
                CHECK_FALSE(std::getline(reader, begin));
                return std::make_unique<int>(42);
            });
    };
    final_words = {0x07230203, 0x10000, 0, 8, 0, 0x10000, 0x12345678};
    const auto result = deferred();
    REQUIRE(result);
    CHECK(*result == 42);
    const auto lines = trace.Lines();
    REQUIRE(lines.size() == 2);
    CHECK(Field(lines[0], "spirv_words") == "7");
    CHECK(Field(lines[1], "spirv_words") == "7");
    CHECK(Field(lines[1], "outcome") == "success");
}

TEST_CASE("Module trace counts words on driver failure without changing exceptions",
          "[video_core][pipeline_build_trace][module_build_trace]") {
    TraceFile trace;
    REQUIRE(trace.file);
    const std::array<u32, 5> words{0x07230203, 0x10000, 0, 1, 0};
    REQUIRE_THROWS_WITH(
        Vulkan::TraceShaderModuleBuild(
            trace.writer, "compute-module", 0x456, words,
            [&](std::span<const u32> driver_words) {
                REQUIRE(driver_words.data() == words.data());
                throw std::runtime_error("module rejected");
            }),
        "module rejected");
    const auto lines = trace.Lines();
    REQUIRE(lines.size() == 2);
    CHECK(Field(lines[0], "spirv_words") == "5");
    CHECK(Field(lines[1], "spirv_words") == "5");
    CHECK(Field(lines[1], "outcome") == "exception");
    CHECK(Field(lines[0], "call") == Field(lines[1], "call"));
}

TEST_CASE("Unknown pipeline word counts remain absent and disabled modules stay transparent",
          "[video_core][pipeline_build_trace][module_build_trace]") {
    TraceFile trace;
    REQUIRE(trace.file);
    Vulkan::TracePipelineBuild(trace.writer, "graphics", 0, {}, false, [] {});
    for (const auto& line : trace.Lines()) {
        CHECK(line.find("spirv_words=") == std::string::npos);
    }
    Vulkan::PipelineBuildTraceWriter disabled{nullptr};
    const std::array<u32, 2> words{0x123, 0x456};
    int calls{};
    const auto& result = Vulkan::TraceShaderModuleBuild(
        disabled, "compute-module", 0x123, words,
        [&](std::span<const u32> driver_words) -> const u32& {
            ++calls;
            CHECK(driver_words.data() == words.data());
            return driver_words[1];
        });
    CHECK(&result == &words[1]);
    CHECK(calls == 1);
    // A disabled call does not consume trace IDs.
    CHECK(disabled.NextCall() == 1);
}
