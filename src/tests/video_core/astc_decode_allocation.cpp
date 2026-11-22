// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using u32 = std::uint32_t;
using u64 = std::uint64_t;
namespace Common {
template <class T> constexpr T DivCeil(T a, T b) { return a / b + (a % b != 0); }
}
namespace vk {
struct Image {
    u32 layers{};
    explicit operator bool() const { return layers != 0; }
    u32 operator*() const { return layers; }
};
}
namespace VideoCommon {
struct ImageFlagBits { static constexpr u32 CpuModified = 1; };
}
#define LOG_ERROR(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#include "astc_decode_budget.inc"

namespace {
void Check(bool value, const char* expression) {
    if (!value) throw std::runtime_error(expression);
}
#define CHECK(x) Check(bool(x), #x)

struct PendingTranscode {
    u64 tick{};
    vk::Image image;
    std::vector<u32> views;
    u32 width{}, height{}, levels{}, layers{}, unused_frames{};
};
struct Device { int GetLogical() const { return 0; } };
u32 MakeStorageView(int, u32 level, u32, int) { return level; }

// Device allocation and pool lookup are observations, never the batch policy.
// The complete production policy/fallback/retry/pending-view construction is included below.
struct Fixture {
    u32 width{4096}, height{4096}, levels{1}, layers{5};
    struct { u32 flags{}; u64 gpu_addr{}; } image;
    Device device;
    static constexpr int DecodeFormat = 0;
    std::vector<u32> free_transcodes;
    std::vector<u32> requests;
    std::vector<u32> lookups;
    bool fail_all{}, fail_while_pool_nonempty{};
    u32 largest_success{UINT32_MAX};
    u32 transitions{};
    PendingTranscode result;

    void TransitionImageLayout(decltype(image)&) { ++transitions; }
    void Run() {
        const auto take_free = [&](u32 count, PendingTranscode& out) {
            lookups.push_back(count);
            auto it = std::find(free_transcodes.begin(), free_transcodes.end(), count);
            if (it == free_transcodes.end()) return false;
            out.layers = count;
            out.image.layers = count;
            free_transcodes.erase(it);
            return true;
        };
        const auto make_decode = [&](u32 count) {
            requests.push_back(count);
            return vk::Image{fail_all || count > largest_success ||
                                     (fail_while_pool_nonempty && !free_transcodes.empty())
                                 ? 0U : count};
        };
#include "astc_decode_allocation.inc"
        result = std::move(pending);
    }
};

void SuccessfulAllocationStillCapped() {
    Fixture f;
    f.Run();
    CHECK(f.requests == std::vector<u32>{3});
    CHECK(f.result.layers == 3);
    CHECK(f.result.views.size() == 1);
}
void ExactBoundary() {
    Fixture f;
    f.layers = 4;
    f.Run();
    CHECK(f.requests == std::vector<u32>{4});
}
void AllHostMipsCounted() {
    Fixture f;
    f.layers = 4;
    f.levels = 13;
    f.Run();
    CHECK(f.requests == std::vector<u32>{2});
    CHECK(f.result.views.size() == 13);
}
void LargeOddArray() {
    Fixture f;
    f.width = f.height = 1365;
    f.levels = 11;
    f.layers = 90;
    f.Run();
    CHECK(f.requests == std::vector<u32>{23});
}
void OversizedSingleLayer() {
    Fixture f;
    f.width = f.height = 9000;
    f.layers = 1;
    f.Run();
    CHECK(f.requests == std::vector<u32>{1});
}
void CappedPoolReuse() {
    Fixture f;
    f.free_transcodes = {3};
    f.Run();
    CHECK(f.requests.empty());
    CHECK(f.result.layers == 3);
    CHECK(f.free_transcodes.empty());
}
void SmallerReuseBeforePoolClear() {
    Fixture f;
    f.fail_all = true;
    f.free_transcodes = {2, 7};
    f.Run();
    CHECK(f.requests == std::vector<u32>{3});
    CHECK(f.result.layers == 2);
    CHECK(f.free_transcodes == std::vector<u32>{7});
    CHECK(f.transitions == 0);
}
void PoolReleaseRetry() {
    Fixture f;
    f.fail_while_pool_nonempty = true;
    f.free_transcodes = {7};
    f.Run();
    CHECK((f.requests == std::vector<u32>{3, 3}));
    CHECK(f.free_transcodes.empty());
    CHECK(f.result.layers == 3);
}
void AllocationFailureFallback() {
    Fixture f;
    f.largest_success = 1;
    f.Run();
    CHECK((f.requests == std::vector<u32>{3, 2, 1}));
    CHECK(f.result.layers == 1);
    CHECK(f.image.flags == 0);
}
void TotalFailureRequestsRetryOnUse() {
    Fixture f;
    f.fail_all = true;
    f.Run();
    CHECK((f.requests == std::vector<u32>{3, 2, 1}));
    CHECK(f.image.flags == VideoCommon::ImageFlagBits::CpuModified);
    CHECK(f.transitions == 1);
    CHECK(!f.result.image);
}
void SmallTextureUnchanged() {
    Fixture f;
    f.width = 70;
    f.height = 38;
    f.levels = 7;
    f.Run();
    CHECK(f.requests == std::vector<u32>{5});
    CHECK(f.result.views.size() == 7);
}
}

int main() {
    struct Case { const char* name; void (*run)(); };
    const Case cases[] = {
        {"successful allocation still capped", SuccessfulAllocationStillCapped},
        {"exact boundary", ExactBoundary}, {"host mip count", AllHostMipsCounted},
        {"large odd array", LargeOddArray}, {"single oversized layer", OversizedSingleLayer},
        {"capped pool reuse", CappedPoolReuse},
        {"smaller reuse before pool clear", SmallerReuseBeforePoolClear},
        {"pool release retry", PoolReleaseRetry}, {"failure fallback", AllocationFailureFallback},
        {"retry next use after total failure", TotalFailureRequestsRetryOnUse},
        {"small texture unchanged", SmallTextureUnchanged},
    };
    unsigned failed{};
    for (const auto& test : cases) {
        try { test.run(); std::printf("PASS %s\n", test.name); }
        catch (const std::exception& e) {
            ++failed;
            std::printf("FAIL %s: %s\n", test.name, e.what());
        }
    }
    std::printf("%zu cases, %u failed\n", std::size(cases), failed);
    return failed ? 1 : 0;
}
