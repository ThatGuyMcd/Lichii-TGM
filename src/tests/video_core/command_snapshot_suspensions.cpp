// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <future>
#include <stdexcept>

#include "core/core.h"
#include "tests/core/system_test_context.h"
#include "video_core/control/channel_state.h"
#include "video_core/dma_pusher.h"
#include "video_core/command_snapshot_refresh.h"
#include "video_core/memory_manager.h"

TEST_CASE("Entries parsed without a pending acquire keep their submit-time snapshot",
          "[video_core][dma_pusher]") {
    ScopedSystemTestContext context;
    Core::System system;
    Tegra::MemoryManager memory_manager{system};
    Tegra::Control::ChannelState channel_state{0};
    Tegra::DmaPusher dma_pusher{system, memory_manager, channel_state};

    const u64 snapshot_suspensions = dma_pusher.suspensions.load();
    for (int entry = 0; entry < 8; ++entry) {
        REQUIRE_FALSE(dma_pusher.puller.HasPendingAcquire());
        REQUIRE(dma_pusher.puller.ResumeAcquire(dma_pusher));
    }
    REQUIRE(dma_pusher.suspensions.load() == snapshot_suspensions);
}

TEST_CASE("Resumed command copies finish before publication and own their staging storage",
          "[video_core][dma_pusher]") {
    std::deque<Tegra::CommandList> lists(2);
    constexpr size_t Words = 16384;
    for (size_t i = 0; i < lists.size(); ++i) {
        lists[i].command_lists.resize(1);
        lists[i].command_lists[0].raw = 0;
        lists[i].command_lists[0].addr.Assign(static_cast<GPUVAddr>(0x1000 + i * 0x1000));
        lists[i].command_lists[0].size.Assign(static_cast<u64>(Words));
        lists[i].submitted_offsets = {0};
        lists[i].submitted_words.resize(Words);
        lists[i].deferred_entries = {0};
    }
    Tegra::CommandSnapshotRefresher refresher;
    std::promise<void> background_read;
    auto background_ready = background_read.get_future();
    refresher.Refresh(lists, 2, 0, [&](GPUVAddr address, void* output, size_t bytes) {
        if (address == 0x1000) {
            background_ready.wait();
        } else {
            background_read.set_value();
        }
        std::memset(output, address == 0x1000 ? 0x11 : 0x22, bytes);
    });
    for (size_t i = 0; i < lists.size(); ++i) {
        REQUIRE(lists[i].snapshot_suspensions == 2);
        REQUIRE(lists[i].deferred_entries.empty());
        const auto* words = reinterpret_cast<const u32*>(lists[i].submitted_words.data());
        REQUIRE(words[0] == (i == 0 ? 0x11111111 : 0x22222222));
        REQUIRE(words[Words - 1] == words[0]);
    }
    SECTION("A failed worker read publishes neither list") {
        REQUIRE_THROWS_AS(refresher.Refresh(lists, 4, 0,
            [](GPUVAddr address, void* output, size_t bytes) {
                if (address == 0x2000) {
                    throw std::runtime_error("read failed");
                }
                std::memset(output, 0x33, bytes);
            }), std::runtime_error);
        REQUIRE(lists[0].snapshot_suspensions == 2);
        REQUIRE(lists[1].snapshot_suspensions == 2);
        REQUIRE(reinterpret_cast<const u32*>(lists[0].submitted_words.data())[0] == 0x11111111);
    }
    SECTION("Entries before the suspended command keep the original snapshot") {
        std::atomic<bool> unexpected_read{};
        refresher.Refresh(lists, 4, 1, [&](GPUVAddr address, void* output, size_t bytes) {
            if (address != 0x2000) {
                unexpected_read.store(true);
            }
            std::memset(output, 0x44, bytes);
        });
        REQUIRE_FALSE(unexpected_read.load());
        REQUIRE(reinterpret_cast<const u32*>(lists[0].submitted_words.data())[0] == 0x11111111);
        REQUIRE(reinterpret_cast<const u32*>(lists[1].submitted_words.data())[0] == 0x44444444);
    }
}

TEST_CASE("Command snapshots reuse released storage without sharing live buffers",
          "[video_core][dma_pusher]") {
    Tegra::CommandWordsAllocator<u32> allocator;
    auto* first = allocator.allocate(1500);
    first[1499] = 0x1234;
    auto* second = allocator.allocate(1500);
    REQUIRE(first != second);
    allocator.deallocate(first, 1500);
    auto* reused = allocator.allocate(1500);
    REQUIRE(reused == first);
    REQUIRE(second != reused);
    allocator.deallocate(second, 1500);
    allocator.deallocate(reused, 1500);
    REQUIRE_THROWS_AS(allocator.allocate(std::numeric_limits<size_t>::max()),
                      std::bad_array_new_length);
}
