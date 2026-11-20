// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <semaphore>
#include <stop_token>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include "common/scope_exit.h"
#include "common/thread_worker.h"
#include "video_core/renderer_vulkan/pipeline_cache_boot.h"

namespace {
using namespace std::chrono_literals;
#include "boot_driver_cache_constants.inc"

struct BootFixture {
    struct Device {
        bool readable{true};
        bool CanReadPipelineCacheDuringBuilds() const { return readable; }
    } device;
    struct Driver {
        std::atomic<unsigned> reads{};
        int Read(size_t* size, void*) {
            ++reads;
            *size = size_t{3} << 30;
            return 0;
        }
    } vulkan_pipeline_cache;
    std::filesystem::path pipeline_cache_filename{"independent-cache.bin"};
    std::filesystem::path vulkan_pipeline_cache_filename{"independent-driver.bin"};
    bool use_vulkan_pipeline_cache{true};
    std::atomic<unsigned> prepared{}, saved{};
    std::atomic_bool block_first{};
    std::binary_semaphore entered{0}, release_save{0};
    size_t pipelines_since_flush{};
    std::chrono::steady_clock::time_point last_flush{17ms};
    std::atomic<size_t> last_cache_size{4096};
    std::atomic_bool flush_in_flight{};
    Common::ThreadWorker serialization_thread{1, "BootCacheContract"};
    static constexpr unsigned CACHE_VERSION = 1;

    ~BootFixture() { serialization_thread.WaitForRequests(); }
    void PreparePrecompiled(const std::filesystem::path&, bool, std::stop_token, int) {
        ++prepared;
    }
    bool SerializeVulkanPipelineCache(const std::filesystem::path&, Driver&, unsigned) {
        ++saved;
        if (block_first.exchange(false)) {
            entered.release();
            release_save.acquire();
        }
        return true;
    }
    void Boot(bool build_at_boot) {
        const bool precache = false;
        const std::stop_token stop_loading{};
        const int callback = 0;
        // The real production statements, including queue captures and state updates.
#include "boot_driver_cache_tail.inc"
    }
    void RuntimeFlush() {
#include "boot_driver_cache_flush.inc"
    }
};

struct MarkerDirectory {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("lichii-boot-cache-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    MarkerDirectory() { REQUIRE(std::filesystem::create_directory(directory)); }
    ~MarkerDirectory() {
        std::error_code error;
        const auto target = std::filesystem::weakly_canonical(directory, error);
        const auto parent = std::filesystem::weakly_canonical(
            std::filesystem::temp_directory_path(), error);
        if (error || target.parent_path() != parent ||
            !target.filename().string().starts_with("lichii-boot-cache-test-")) {
            return;
        }
        std::filesystem::remove_all(target, error);
    }
};
} // namespace

TEST_CASE("Recovery boot never exports an unchanged driver cache", "[boot_driver_cache]") {
    MarkerDirectory temp;
    const auto file = temp.directory / "independent-cache.bin";
    {
        Vulkan::PipelineCacheBoot interrupted{file};
        REQUIRE(interrupted.BuildAtBoot());
        REQUIRE(interrupted.Begin());
        Vulkan::PipelineCacheBoot busy{file};
        REQUIRE(busy.Busy());
        BootFixture waiting;
        waiting.Boot(busy.BuildAtBoot());
        waiting.serialization_thread.WaitForRequests();
        CHECK(waiting.saved == 0);
        CHECK(waiting.vulkan_pipeline_cache.reads == 0);
    }
    {
        Vulkan::PipelineCacheBoot recovery{file};
        REQUIRE_FALSE(recovery.BuildAtBoot());
        REQUIRE(recovery.Begin());
        BootFixture fixture;
        fixture.Boot(recovery.BuildAtBoot());
        fixture.serialization_thread.WaitForRequests();
        CHECK(fixture.prepared == 0);
        CHECK(fixture.saved == 0);
        CHECK(fixture.vulkan_pipeline_cache.reads == 0);
        CHECK(fixture.last_cache_size == 4096);
        CHECK(fixture.last_flush == std::chrono::steady_clock::time_point{17ms});
        CHECK_FALSE(fixture.flush_in_flight);
        recovery.Complete();
    }
    Vulkan::PipelineCacheBoot normal{file};
    REQUIRE(normal.BuildAtBoot());
    for (const bool enabled : {false, true}) {
        for (const bool readable : {false, true}) {
            BootFixture fixture;
            fixture.use_vulkan_pipeline_cache = enabled;
            fixture.device.readable = readable;
            fixture.Boot(normal.BuildAtBoot());
            fixture.serialization_thread.WaitForRequests();
            CHECK(fixture.prepared == 1);
            CHECK(fixture.saved == unsigned(enabled && readable));
            CHECK(fixture.vulkan_pipeline_cache.reads == unsigned(enabled && readable));
        }
    }
}

TEST_CASE("A slow boot export runs on the drained worker without blocking launch",
          "[boot_driver_cache]") {
    BootFixture fixture;
    fixture.block_first = true;
    std::binary_semaphore returned{0};
    bool returned_early = false;
    {
        // Release even if a fatal assertion unwinds while the deliberately slow save waits.
        std::jthread loader([&] { fixture.Boot(true); returned.release(); });
        // Loader must be joined after releasing the synthetic driver call on every path.
        SCOPE_EXIT { fixture.release_save.release(); loader.join(); };
        REQUIRE(fixture.entered.try_acquire_for(2s));
        returned_early = returned.try_acquire_for(500ms);
        CHECK(returned_early);
        if (returned_early) {
            CHECK(fixture.flush_in_flight);
            CHECK(fixture.vulkan_pipeline_cache.reads == 0);
            CHECK(fixture.last_cache_size == 4096);
            CHECK(fixture.last_flush != std::chrono::steady_clock::time_point{17ms});
            // Make the periodic path otherwise eligible: the in-flight guard must suppress it.
            fixture.pipelines_since_flush = VULKAN_CACHE_FLUSH_PIPELINES;
            fixture.last_flush = {};
            fixture.RuntimeFlush();
        }
    }
    fixture.serialization_thread.WaitForRequests();
    CHECK(fixture.prepared == 1);
    CHECK(fixture.saved == 1);
    CHECK(fixture.vulkan_pipeline_cache.reads == 1);
    CHECK(fixture.last_cache_size == (size_t{3} << 30));
    CHECK_FALSE(fixture.flush_in_flight);
    // Later runtime persistence remains enabled after the boot snapshot drains.
    fixture.pipelines_since_flush = VULKAN_CACHE_FLUSH_PIPELINES;
    fixture.last_flush = {};
    fixture.RuntimeFlush();
    fixture.serialization_thread.WaitForRequests();
    CHECK(fixture.saved == 2);
    CHECK(fixture.vulkan_pipeline_cache.reads == 2);
    CHECK_FALSE(fixture.flush_in_flight);
}
