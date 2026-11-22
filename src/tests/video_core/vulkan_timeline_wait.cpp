// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {
using u64 = std::uint64_t;
constexpr int VK_ERROR_DEVICE_LOST = -4;
constexpr int VK_ERROR_OUT_OF_DEVICE_MEMORY = -2;

namespace vk {
class Exception : public std::runtime_error {
public:
    explicit Exception(int result_) : std::runtime_error{"Synthetic Vulkan error"}, result{result_} {}

    int GetResult() const {
        return result;
    }

private:
    int result;
};
} // namespace vk

namespace VideoCommon::Nx2Profile {
enum class Zone { GpuWait };
struct Scope {
    explicit Scope(Zone) {}
};
} // namespace VideoCommon::Nx2Profile

struct Event {
    u64 value{};
    int error{};

    u64 Result() const {
        if (error != 0) {
            throw vk::Exception{error};
        }
        return value;
    }
};

struct Timeline {
    bool present{true};
    std::vector<Event> counters;
    std::vector<Event> waits;
    std::vector<u64> requested_ticks;
    std::vector<u64> timeouts;
    size_t reads{};

    explicit operator bool() const {
        return present;
    }

    u64 GetCounter() {
        return counters.at(reads++).Result();
    }

    bool Wait(u64 tick, u64 timeout = (std::numeric_limits<u64>::max)()) {
        requested_ticks.push_back(tick);
        timeouts.push_back(timeout);
        return waits.at(timeouts.size() - 1).Result() != 0;
    }
};

struct Device {
    int losses{};

    void ReportLoss() {
        ++losses;
    }
};

struct MasterSemaphore {
    Timeline semaphore;
    Device device;
    std::atomic<u64> gpu_tick{};

    bool IsFree(u64 tick) const {
        return gpu_tick.load(std::memory_order_acquire) >= tick;
    }

    void Refresh();
    void Wait(u64 tick);
};

#define LOG_CRITICAL(...) static_cast<void>(0)
#include "vulkan_timeline_wait.inc"
#undef LOG_CRITICAL

TEST_CASE("Timeline waits avoid Vulkan calls when the tick is already known") {
    MasterSemaphore master;
    master.gpu_tick = 5;
    master.Wait(5);
    CHECK(master.semaphore.reads == 0);
    CHECK(master.semaphore.timeouts.empty());

    master.semaphore.present = false;
    master.Wait(5);
    CHECK(master.semaphore.reads == 0);
    CHECK(master.semaphore.timeouts.empty());
}

TEST_CASE("A first counter query can complete a wait without waiting on the driver") {
    MasterSemaphore master;
    master.semaphore.counters = {{7}};
    master.Wait(5);
    CHECK(master.IsFree(7));
    CHECK(master.semaphore.reads == 1);
    CHECK(master.semaphore.timeouts.empty());
}

TEST_CASE("A timeout with a completed counter does not wait again") {
    MasterSemaphore master;
    master.semaphore.counters = {{2}, {7}};
    master.semaphore.waits = {{0}};
    REQUIRE_NOTHROW(master.Wait(5));
    CHECK(master.IsFree(7));
    CHECK(master.semaphore.reads == 2);
    CHECK(master.semaphore.requested_ticks == std::vector<u64>{5});
    CHECK(master.semaphore.timeouts == std::vector<u64>{250'000'000});
}

TEST_CASE("A partial counter keeps waiting for the originally requested tick") {
    MasterSemaphore master;
    master.semaphore.counters = {{1}, {3}, {7}};
    master.semaphore.waits = {{0}, {1}};
    master.Wait(7);
    CHECK(master.IsFree(7));
    CHECK(master.semaphore.reads == 3);
    CHECK(master.semaphore.requested_ticks == std::vector<u64>{7, 7});
    CHECK(master.semaphore.timeouts == std::vector<u64>{250'000'000, 250'000'000});
}

TEST_CASE("Device loss is reported from timeline waits and every counter query") {
    MasterSemaphore master;
    const Event lost{.error = VK_ERROR_DEVICE_LOST};
    SECTION("Initial query") {
        master.semaphore.counters = {lost};
    }
    SECTION("Driver wait") {
        master.semaphore.counters = {{0}};
        master.semaphore.waits = {lost};
    }
    SECTION("Timeout query") {
        master.semaphore.counters = {{0}, lost};
        master.semaphore.waits = {{0}};
    }
    SECTION("Successful wait query") {
        master.semaphore.counters = {{0}, lost};
        master.semaphore.waits = {{1}};
    }
    CHECK_THROWS_AS(master.Wait(7), vk::Exception);
    CHECK(master.device.losses == 1);
}

TEST_CASE("A wait error other than device loss is propagated without a loss report") {
    MasterSemaphore master;
    master.semaphore.counters = {{0}};
    master.semaphore.waits = {{.error = VK_ERROR_OUT_OF_DEVICE_MEMORY}};
    CHECK_THROWS_AS(master.Wait(7), vk::Exception);
    CHECK(master.device.losses == 0);
}
} // namespace
