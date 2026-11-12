// SPDX-License-Identifier: GPL-3.0-or-later

#include <numeric>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "video_core/nx2_profile.h"

namespace {
namespace Profile = VideoCommon::Nx2Profile;
using Profile::Zone;

struct ScopedThreadData {
    ScopedThreadData() : saved{std::exchange(Profile::t_data, {})} {}
    ~ScopedThreadData() {
        Profile::t_data = saved;
    }
    Profile::ThreadData saved;
};

constexpr size_t Index(Zone zone) {
    return static_cast<size_t>(zone);
}
} // namespace

TEST_CASE("Disabled profile scopes preserve an existing accounting context", "[nx2_profile]") {
    const ScopedThreadData context;
    auto& data = Profile::t_data;
    data.current = Zone::Draw;
    data.depth = 3;
    data.child_ns = 17;
    data.count[Index(Zone::Draw)] = 4;
    data.exclusive_ns[Index(Zone::Draw)] = 23;
    data.gpu_wait_count[Index(Zone::Draw)] = 5;
    data.gpu_wait_ns[Index(Zone::Draw)] = 31;
    const auto before = data;
    {
        const Profile::Scope disabled{Zone::GpuWait, false};
        CHECK(data.depth == before.depth);
        CHECK(data.current == before.current);
    }
    CHECK(data.current == before.current);
    CHECK(data.depth == before.depth);
    CHECK(data.child_ns == before.child_ns);
    CHECK(data.count == before.count);
    CHECK(data.exclusive_ns == before.exclusive_ns);
    CHECK(data.gpu_wait_count == before.gpu_wait_count);
    CHECK(data.gpu_wait_ns == before.gpu_wait_ns);
}

TEST_CASE("Enabled nested profile scopes retain parent and GPU wait accounting", "[nx2_profile]") {
    const ScopedThreadData context;
    auto& data = Profile::t_data;
    {
        const Profile::Scope draw{Zone::Draw, true};
        CHECK(data.depth == 1);
        CHECK(data.current == Zone::Draw);
        {
            const Profile::Scope configure{Zone::Configure, true};
            CHECK(data.depth == 2);
            CHECK(data.current == Zone::Configure);
            {
                const Profile::Scope wait{Zone::GpuWait, true};
                CHECK(data.depth == 3);
                CHECK(data.current == Zone::GpuWait);
            }
            CHECK(data.depth == 2);
            CHECK(data.current == Zone::Configure);
            CHECK(data.gpu_wait_count[Index(Zone::Configure)] == 1);
        }
        CHECK(data.depth == 1);
        CHECK(data.current == Zone::Draw);
    }
    CHECK(data.depth == 0);
    CHECK(data.current == Zone::Other);
    CHECK(data.count[Index(Zone::Draw)] == 1);
    CHECK(data.count[Index(Zone::Configure)] == 1);
    CHECK(data.count[Index(Zone::GpuWait)] == 1);
    CHECK(data.gpu_wait_ns[Index(Zone::Configure)] == data.exclusive_ns[Index(Zone::GpuWait)]);
    CHECK(std::accumulate(data.exclusive_ns.begin(), data.exclusive_ns.end(), u64{}) == data.child_ns);
}

TEST_CASE("An untimed child does not change its timed parent's wait attribution", "[nx2_profile]") {
    const ScopedThreadData context;
    auto& data = Profile::t_data;
    {
        const Profile::Scope draw{Zone::Draw, true};
        {
            const Profile::Scope disabled{Zone::GpuWait, false};
            CHECK(data.depth == 1);
            CHECK(data.current == Zone::Draw);
        }
        CHECK(data.count[Index(Zone::GpuWait)] == 0);
        CHECK(data.gpu_wait_count[Index(Zone::Draw)] == 0);
    }
    CHECK(data.count[Index(Zone::Draw)] == 1);
    CHECK(data.depth == 0);
    CHECK(data.current == Zone::Other);
}
