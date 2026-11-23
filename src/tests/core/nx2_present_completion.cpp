// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "common/scope_exit.h"
#include "core/core_timing.h"
#include "core/hle/service/nvdrv/nvdata.h"
#include "core/hle/service/nx2_present_queue.h"
#include "video_core/framebuffer_config.h"

namespace {
using namespace std::chrono_literals;
using Token = std::shared_ptr<std::atomic<bool>>;

struct Fixture {
    Fixture() {
        timing.SetMulticore(false);
        timing.Initialize([] {});
    }
    void Advance() {
        timing.AddTicks(1'000'000'000);
        timing.Advance();
    }
    Core::Timing::CoreTiming timing;
    Tegra::Host1x::SyncpointManager syncpoints;
};

auto Enqueue(Service::Nx2PresentReleaseQueue& queue, std::chrono::nanoseconds deadline,
             u32 fence_id, u32 fence_value,
             const Token& completion) {
    auto* m_release_queue = &queue;
    // Use the actual service-to-queue token forwarding, including its absence on the baseline.
#include "present_service_enqueue.inc"
    return value;
}

// Only hardware boundaries are fake: the complete RequestComposite body is source-linked.
struct GpuProbe {
    struct Host {
        Tegra::Host1x::SyncpointManager syncpoints;
        auto& GetSyncpointManager() { return syncpoints; }
    };
    struct System {
        Host host;
        Host& Host1x() { return host; }
    } system;
    struct Renderer {
        size_t calls{};
        std::function<void()> consuming;
        void Composite(const std::vector<Tegra::FramebufferConfig>&) {
            ++calls;
            if (consuming) consuming();
        }
    } actual_renderer;
    Renderer* renderer{&actual_renderer};
    struct Thread {
        size_t ticks{};
        void TickGPU(bool) { ++ticks; }
    } gpu_thread;
    bool is_async{true};
    std::mutex request_swap_mutex;
    std::vector<size_t> request_swap_counters;
    std::deque<size_t> free_swap_counters;
    std::deque<std::function<void()>> requests;
    u64 pending_composite_fence{};
    u64 RequestSyncOperation(std::function<void()> request) {
        requests.push_back(std::move(request));
        return requests.size();
    }
    void Run() {
        while (!requests.empty()) {
            auto request = std::move(requests.front());
            requests.pop_front();
            request();
        }
    }
    void Submit(std::vector<Tegra::FramebufferConfig>&& layers,
                std::vector<Service::Nvidia::NvFence>&& fences,
                std::function<void()> on_composited = {}) {
#include "present_gpu_body.inc"
    }
    void RequestComposite(std::vector<Tegra::FramebufferConfig>&& layers,
                          std::vector<Service::Nvidia::NvFence>&& fences,
                          std::function<void()> on_composited = {}) {
        Submit(std::move(layers), std::move(fences), std::move(on_composited));
    }
};

void FailedServiceLookup(const Token& completion) {
    // This prefix is executed before every image/memory/mapping lookup in Composite.
#include "present_service_prefix.inc"
    return;
}

void SubmitService(GpuProbe& gpu, const Token& completion) {
#include "present_service_prefix.inc"
    struct SystemBoundary {
        GpuProbe& gpu;
        GpuProbe& GPU() { return gpu; }
    } system{gpu};
    std::vector<Tegra::FramebufferConfig> layers;
    std::vector<Service::Nvidia::NvFence> fences;
#include "present_service_dispatch.inc"
}
} // namespace

TEST_CASE("Native release waits for actual compositor consumption", "[nx2_present_completion]") {
    Fixture f;
    Service::Nx2PresentReleaseQueue queue{f.timing, f.syncpoints, 1};
    auto completion = std::make_shared<std::atomic<bool>>(false);
    GpuProbe gpu;
    f.syncpoints.IncrementGuest(2);
    REQUIRE(Enqueue(queue, 0ns, 2, 1, completion) == 1);
    SubmitService(gpu, completion);
    f.Advance();
    CHECK(f.syncpoints.GetGuestSyncpointValue(1) == 0);
    CHECK(gpu.actual_renderer.calls == 0);
    gpu.actual_renderer.consuming = [&] {
        CHECK_FALSE(completion->load(std::memory_order_acquire));
        f.Advance();
        CHECK(f.syncpoints.GetGuestSyncpointValue(1) == 0);
    };
    gpu.Run();
    REQUIRE(gpu.actual_renderer.calls == 1);
    CHECK(completion->load(std::memory_order_acquire));
    f.Advance();
    CHECK(f.syncpoints.GetGuestSyncpointValue(1) == 1);
}

TEST_CASE("Compositor completion preserves deadline and guest fence controls", "[nx2_present_completion]") {
    Fixture f;
    Service::Nx2PresentReleaseQueue queue{f.timing, f.syncpoints, 1};
    auto completion = std::make_shared<std::atomic<bool>>(true);
    REQUIRE(Enqueue(queue, 10ms, 2, 1, completion) == 1);
    f.timing.Advance();
    CHECK(f.syncpoints.GetGuestSyncpointValue(1) == 0);
    f.Advance();
    CHECK(f.syncpoints.GetGuestSyncpointValue(1) == 0);
    f.syncpoints.IncrementGuest(2);
    f.Advance();
    CHECK(f.syncpoints.GetGuestSyncpointValue(1) == 1);
}

TEST_CASE("GPU composite callbacks follow the last fence and renderer return", "[nx2_present_completion]") {
    GpuProbe gpu;
    size_t completed{};
    gpu.actual_renderer.consuming = [&] { CHECK(completed == 0); };
    gpu.Submit({}, {{2, 1}, {3, 1}}, [&] { ++completed; });
    gpu.Run();
    CHECK(completed == 0);
    gpu.system.host.syncpoints.IncrementGuest(3);
    CHECK(completed == 0);
    CHECK(gpu.actual_renderer.calls == 0);
    gpu.system.host.syncpoints.IncrementGuest(2);
    CHECK(gpu.actual_renderer.calls == 1);
    CHECK(completed == 1);
    // Already-reached fences synchronously execute during request processing.
    gpu.actual_renderer.consuming = [&] { CHECK(completed == 1); };
    gpu.Submit({}, {{2, 1}}, [&] { ++completed; });
    gpu.Run();
    CHECK(gpu.actual_renderer.calls == 2);
    CHECK(completed == 2);
    // Existing callers without a callback retain normal asynchronous composition.
    gpu.actual_renderer.consuming = {};
    gpu.Submit({}, {});
    CHECK(gpu.actual_renderer.calls == 2);
    gpu.Run();
    CHECK(gpu.actual_renderer.calls == 3);
    CHECK(completed == 2);
}

TEST_CASE("Late composite callbacks own only their completion token", "[nx2_present_completion]") {
    Fixture f;
    GpuProbe gpu;
    std::weak_ptr<std::atomic<bool>> weak;
    {
        Service::Nx2PresentReleaseQueue queue{f.timing, f.syncpoints, 1};
        auto completion = std::make_shared<std::atomic<bool>>(false);
        weak = completion;
        REQUIRE(Enqueue(queue, 0ns, ~0U, 0, completion) == 1);
        SubmitService(gpu, completion);
    }
    CHECK(f.syncpoints.GetGuestSyncpointValue(1) == 1);
    CHECK_FALSE(weak.expired());
    gpu.Run();
    CHECK(weak.expired());
    f.Advance();
    CHECK(f.syncpoints.GetGuestSyncpointValue(1) == 1);
}

TEST_CASE("Native completion tokens retain FIFO ordering and bounded pending work",
          "[nx2_present_completion]") {
    Fixture f;
    Service::Nx2PresentReleaseQueue queue{f.timing, f.syncpoints, 1};
    auto first = std::make_shared<std::atomic<bool>>(false);
    auto later = std::make_shared<std::atomic<bool>>(true);
    REQUIRE(Enqueue(queue, 0ns, ~0U, 0, first) == 1);
    for (u32 value = 2; value <= Service::Nx2PresentReleaseQueue::Capacity; ++value) {
        REQUIRE(Enqueue(queue, 0ns, ~0U, 0, later) == value);
    }
    CHECK_FALSE(Enqueue(queue, 0ns, ~0U, 0, later).has_value());
    f.Advance();
    CHECK(f.syncpoints.GetGuestSyncpointValue(1) == 0);
    first->store(true, std::memory_order_release);
    f.Advance();
    CHECK(f.syncpoints.GetGuestSyncpointValue(1) == 15);
    CHECK_FALSE(f.timing.Advance().has_value());
    REQUIRE(Enqueue(queue, 0ns, ~0U, 0, later) == 16);
    f.Advance();
    CHECK(f.syncpoints.GetGuestSyncpointValue(1) == 16);
}

TEST_CASE("Failed native image lookup cannot orphan a release", "[nx2_present_completion]") {
    Fixture f;
    Service::Nx2PresentReleaseQueue queue{f.timing, f.syncpoints, 1};
    auto completion = std::make_shared<std::atomic<bool>>(false);
    REQUIRE(Enqueue(queue, 0ns, ~0U, 0, completion) == 1);
    FailedServiceLookup(completion);
    CHECK(completion->load(std::memory_order_acquire));
    f.Advance();
    CHECK(f.syncpoints.GetGuestSyncpointValue(1) == 1);
}
