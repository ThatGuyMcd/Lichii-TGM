// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "tests/video_core/oot_perf_null_rasterizer.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/memory_manager.h"

namespace Tegra::Engines {
struct KeplerUploadLifetimeTestAccess {
    static size_t Pending(const KeplerCompute& engine) { return engine.uploads.size(); }
    static bool LaunchNative(KeplerCompute& engine, bool valid = true) {
        NativeQmdScheduler::Descriptor qmd{};
        qmd[12] = qmd[13] = qmd[14] = 1;
        qmd[18] = valid ? 1U << 16 : 0;
        qmd[19] = (1U << 16) | 1;
        return engine.DispatchNativeCompute(qmd);
    }
    static void SetNative(KeplerCompute& engine, bool native) {
        engine.native_mode = native;
        engine.upload_state.SetNativePartialTransfers(native);
    }
};
} // namespace Tegra::Engines

namespace {
class UploadRasterizer final : public OotPerfNullRasterizer {
public:
    void AccelerateInlineToMemory(GPUVAddr address, size_t size,
                                  std::span<const u8> bytes) override {
        REQUIRE(size == sizeof(u32));
        u32 value{};
        std::memcpy(&value, bytes.data(), sizeof(value));
        addresses.push_back(address);
        words.push_back(value);
    }
    std::vector<GPUVAddr> addresses;
    std::vector<u32> words;
};
} // namespace

TEST_CASE("Native compute consumes upload provenance without dropping upload data",
          "[video_core][oot_perf][kepler_upload]") {
    ScopedSystemTestContext context;
    const u64 previous_program = Settings::GetCurrentProgramID();
    Settings::SetCurrentProgramID(0);
    SCOPE_EXIT { Settings::SetCurrentProgramID(previous_program); };
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    UploadRasterizer rasterizer;
    Tegra::MemoryManager memory{system, device_memory, 40, 1ULL << 34, 16};
    memory.BindRasterizer(&rasterizer);
    Tegra::Engines::KeplerCompute engine{memory};
    engine.BindRasterizer(&rasterizer);
    using Access = Tegra::Engines::KeplerUploadLifetimeTestAccess;
    engine.regs = {};
    engine.regs.upload.line_length_in = sizeof(u32);
    engine.regs.upload.line_count = 1;
    engine.regs.upload.dest.address_low = 0x100000;
    Access::SetNative(engine, true);
    for (u32 batch = 0; batch < 12; ++batch) {
        for (u32 i = 0; i < 8; ++i) {
            const u32 word = batch * 8 + i;
            engine.CallMethod(system, KEPLER_COMPUTE_REG_INDEX(exec_upload), 1, true);
            engine.CallMethod(system, KEPLER_COMPUTE_REG_INDEX(data_upload), word, true);
            REQUIRE(Access::Pending(engine) == i + 1);
            REQUIRE(rasterizer.words.back() == word);
        }
        REQUIRE(Access::LaunchNative(engine));
        REQUIRE(Access::Pending(engine) == 0);
    }
    REQUIRE(rasterizer.dispatches == 12);
    REQUIRE(rasterizer.words.size() == 96);
    for (size_t i = 0; i < rasterizer.words.size(); ++i) {
        REQUIRE(rasterizer.words[i] == i);
        REQUIRE(rasterizer.addresses[i] == 0x100000);
    }

    // A rejected QMD still consumes the upload epoch; no whole-session history survives it.
    engine.CallMethod(system, KEPLER_COMPUTE_REG_INDEX(exec_upload), 1, true);
    REQUIRE_FALSE(Access::LaunchNative(engine, false));
    REQUIRE(Access::Pending(engine) == 0);

    // Legacy launches retain all provenance until their launch, including more than 4096 records.
    Access::SetNative(engine, false);
    for (size_t i = 0; i < 4100; ++i) {
        engine.CallMethod(system, KEPLER_COMPUTE_REG_INDEX(exec_upload), 1, true);
    }
    REQUIRE(Access::Pending(engine) == 4100);
    constexpr GPUVAddr launch = 0x200000;
    constexpr DAddr backing = 0x4000;
    device_memory.KeepWrites(backing, 0x1000);
    memory.Map(launch, backing, 0x1000, Tegra::PTEKind::PITCH, false);
    Tegra::Engines::KeplerCompute::LaunchParams params{};
    REQUIRE(device_memory.WriteKept(backing, &params, sizeof(params)));
    engine.regs.launch_desc_loc.address = launch >> 8;
    engine.CallMethod(system, KEPLER_COMPUTE_REG_INDEX(launch), 0, true);
    REQUIRE(Access::Pending(engine) == 0);
    REQUIRE(rasterizer.dispatches == 13);
}
