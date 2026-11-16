// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <chrono>
#include <limits>
#include <thread>
#include <tuple>

#include <catch2/catch_test_macros.hpp>

#include "video_core/texture_cache/download_landing.h"

namespace {

template <typename Predicate>
bool WaitUntil(Predicate&& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

} // namespace

TEST_CASE("Texture writeback preserves pending CPU writes and disjoint boundaries",
          "[oot_perf][texture_landing]") {
    for (const auto& [address, size, expect_store] :
         {std::tuple<DAddr, size_t, bool>{0x1000, 1, false}, {0x10ff, 1, false},
          {0x0fff, 2, false}, {0x0fff, 1, true}, {0x1100, 8, true}, {0x1000, 0, true}}) {
        bool stored = false;
        const bool landed = VideoCommon::LandTextureDownload(
            0x1000, 0x1100, [&](const auto& visit) { visit(address, size); },
            [&] { stored = true; });
        CHECK(landed == expect_store);
        CHECK(stored == expect_store);
    }
    bool stored = false;
    CHECK_FALSE(VideoCommon::LandTextureDownload(
        0, ~DAddr{0}, [](const auto& visit) { visit(0x5000, 1); }, [&] { stored = true; }));
    CHECK_FALSE(stored);
    // Saturating overlap logic must not turn a wrapping write into a disjoint one.
    CHECK_FALSE(VideoCommon::LandTextureDownload(
        0x1000, 0x1100,
        [](const auto& visit) { visit(0xfff, (std::numeric_limits<size_t>::max)()); }, [] {}));
}

TEST_CASE("A CPU write collected after the texture landing check stores last",
          "[oot_perf][texture_landing]") {
    std::atomic<bool> begin{};
    std::atomic<bool> done{};
    std::atomic<bool> quit{};
    std::atomic<int> bytes{1};
    std::thread writer([&] {
        if (!WaitUntil([&] { return begin.load() || quit.load(); }) || quit.load()) {
            return;
        }
        VideoCore::DownloadLanding::Await(0x1050, 4);
        bytes.store(3);
        done.store(true);
    });
    const u64 waits = VideoCore::DownloadLanding::Waits();
    bool observed_wait = false;
    const bool landed = VideoCommon::LandTextureDownload(
        0x1000, 0x1100,
        [&](const auto&) {
            begin.store(true);
            observed_wait = WaitUntil([&] {
                return VideoCore::DownloadLanding::Waits() > waits || done.load();
            });
        },
        [&] {
            CHECK_FALSE(done.load());
            bytes.store(2);
        });
    quit.store(true);
    writer.join();
    CHECK(landed);
    CHECK(observed_wait);
    CHECK(VideoCore::DownloadLanding::Waits() > waits);
    CHECK(done.load());
    CHECK(bytes.load() == 3);
}

TEST_CASE("Independent texture and buffer landings cannot cancel each other's guard",
          "[oot_perf][texture_landing]") {
    std::atomic<bool> done{};
    std::thread writer;
    const u64 waits = VideoCore::DownloadLanding::Waits();
    bool blocked = false;
    {
        VideoCore::DownloadLanding texture{
            0x1000, 0x1100, VideoCore::DownloadLanding::Source::TextureCache};
        {
            VideoCore::DownloadLanding buffer{0x3000, 0x3100};
            VideoCore::DownloadLanding::Await(0x4000, 1);
        }
        writer = std::thread([&] {
            VideoCore::DownloadLanding::Await(0x1050, 1);
            done.store(true);
        });
        blocked = WaitUntil([&] {
            return VideoCore::DownloadLanding::Waits() > waits || done.load();
        });
        CHECK_FALSE(done.load());
    }
    writer.join();
    CHECK(blocked);
    CHECK(VideoCore::DownloadLanding::Waits() > waits);
    CHECK(done.load());
}
