// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "common/scope_exit.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "tests/video_core/oot_perf_null_rasterizer.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/memory_manager.h"
#include "video_core/ocarina_upscale.h"
#include "video_core/oot_network.h"

namespace {
class NetworkRasterizer final : public OotPerfNullRasterizer {
public:
    VideoCommon::OotNetwork::BypassState* OotNetworkBypass() override {
        return temporal ? nullptr : &state;
    }
    bool AccelerateSurfaceCopy(const Tegra::Engines::Fermi2D::Surface& source,
                               const Tegra::Engines::Fermi2D::Surface& destination,
                               const Tegra::Engines::Fermi2D::Config& config) override {
        last_source = source;
        last_destination = destination;
        last_config = config;
        ++copies;
        return copy_success;
    }
    VideoCommon::OotNetwork::BypassState state;
    Tegra::Engines::Fermi2D::Surface last_source{}, last_destination{};
    Tegra::Engines::Fermi2D::Config last_config{};
    size_t copies{};
    bool copy_success{true};
    bool temporal{};
};
} // namespace

TEST_CASE("OOT network bypass reads the current output pass input and recovers missing outputs",
          "[video_core][oot_perf][oot_network]") {
    using namespace VideoCommon::OotNetwork;
    using namespace Tegra::Texture;
    ScopedSystemTestContext context;
    auto& setting = Settings::values.oot_anti_aliasing;
    const auto previous = setting.GetValue();
    SCOPE_EXIT { setting.SetValue(previous); };
    setting.SetValue(Settings::OotAntiAliasing::NoSharpening);
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    NetworkRasterizer rasterizer;
    Tegra::MemoryManager memory{system, device_memory, 40, 1ULL << 34, 16};
    memory.BindRasterizer(&rasterizer);
    constexpr GPUVAddr gpu = 0x100000;
    constexpr DAddr backing = 0x10000;
    device_memory.KeepWrites(backing, 0x1000);
    memory.Map(gpu, backing, 0x1000, Tegra::PTEKind::PITCH, false);
    Tegra::Engines::KeplerCompute compute{memory};
    compute.BindRasterizer(&rasterizer);
    compute.launch_description.const_buffer_enable_mask.Assign(1);
    auto& cb = compute.launch_description.const_buffer_config[0];
    cb.address_low = gpu;
    cb.size.Assign(0x100);
    compute.regs.tic.address_low = gpu + 0x200;
    compute.regs.tic.limit = 1;
    const u32 input_handle = 1;
    const u32 output_handle = 0;
    REQUIRE(device_memory.WriteKept(backing + InputImageHandleOffset, &input_handle, sizeof(u32)));
    REQUIRE(device_memory.WriteKept(backing + OutputImageHandleOffset, &output_handle, sizeof(u32)));
    TICEntry output{};
    output.format.Assign(TextureFormat::B10G11R11);
    output.header_version.Assign(TICHeaderVersion::BlockLinear);
    output.texture_type.Assign(TextureType::Texture2D);
    output.address_low = 0x200000;
    output.width_minus_one.Assign(1919);
    output.height_minus_1.Assign(1079);
    TICEntry input = output;
    input.address_low = 0x300000;
    input.format.Assign(TextureFormat::E5B9G9R9);
    const auto publish = [&] {
        REQUIRE(device_memory.WriteKept(backing + 0x200, &output, sizeof(output)));
        REQUIRE(device_memory.WriteKept(backing + 0x200 + sizeof(input), &input, sizeof(input)));
    };
    publish();

    REQUIRE_FALSE(compute.BypassOotNetwork(ProgramId, PrepassProgramKey));
    REQUIRE_FALSE(compute.BypassOotNetwork(ProgramId, NetworkProgramKey));
    REQUIRE(compute.BypassOotNetwork(ProgramId, OutputProgramKey));
    REQUIRE(rasterizer.last_source.addr_lower == 0x300000);
    REQUIRE(rasterizer.last_source.format == Tegra::RenderTargetFormat::B10G11R11_FLOAT);
    REQUIRE(rasterizer.last_config.filter == Tegra::Engines::Fermi2D::Filter::Point);
    REQUIRE(compute.BypassOotNetwork(ProgramId, PrepassProgramKey));
    REQUIRE(compute.BypassOotNetwork(ProgramId, NetworkProgramKey));
    // A later frame swaps its input without a matching known tonemap draw.
    input.address_low = 0x400000;
    publish();
    REQUIRE(compute.BypassOotNetwork(ProgramId, OutputProgramKey));
    REQUIRE(rasterizer.last_source.addr_lower == 0x400000);
    REQUIRE(compute.BypassOotNetwork(ProgramId, PrepassProgramKey));
    REQUIRE_FALSE(compute.BypassOotNetwork(ProgramId, 0x1234));
    REQUIRE_FALSE(compute.BypassOotNetwork(ProgramId, PrepassProgramKey));
    REQUIRE_FALSE(compute.BypassOotNetwork(ProgramId, NetworkProgramKey));
    REQUIRE(compute.BypassOotNetwork(ProgramId, OutputProgramKey));

    rasterizer.copy_success = false;
    REQUIRE_FALSE(compute.BypassOotNetwork(ProgramId, OutputProgramKey));
    REQUIRE_FALSE(compute.BypassOotNetwork(ProgramId, PrepassProgramKey));
    rasterizer.copy_success = true;
    const size_t before_invalid = rasterizer.copies;
    input.address_low = output.address_low;
    publish();
    REQUIRE_FALSE(compute.BypassOotNetwork(ProgramId, OutputProgramKey));
    input.address_low = 0x400000;
    input.format.Assign(TextureFormat::A8B8G8R8);
    publish();
    REQUIRE_FALSE(compute.BypassOotNetwork(ProgramId, OutputProgramKey));
    input.format.Assign(TextureFormat::B10G11R11);
    input.header_version.Assign(TICHeaderVersion::Pitch);
    publish();
    REQUIRE_FALSE(compute.BypassOotNetwork(ProgramId, OutputProgramKey));
    input.header_version.Assign(TICHeaderVersion::BlockLinear);
    publish();
    cb.size.Assign(InputImageHandleOffset);
    REQUIRE_FALSE(compute.BypassOotNetwork(ProgramId, OutputProgramKey));
    cb.size.Assign(0x100);
    rasterizer.temporal = true;
    REQUIRE_FALSE(compute.BypassOotNetwork(ProgramId, OutputProgramKey));
    REQUIRE(rasterizer.copies == before_invalid);
}

TEST_CASE("OOT pass recognition survives lowering while retaining older variants",
          "[video_core][oot_perf][oot_network]") {
    using namespace VideoCommon;
    using enum OcarinaUpscalePass;
    REQUIRE(FindOcarinaUpscalePass(0x6f9367bd41dc0b58ULL, 0) == ThreeQuarter);
    REQUIRE(FindOcarinaUpscalePass(0xda6df3e54c716df2ULL, 0) == Pass1440p);
    REQUIRE(FindOcarinaUpscalePass(0xe4cda162ff6009e2ULL, 0) == Pass1440p);
    for (const u64 hash : {0xe40f5efb5480e692ULL, 0xef542df87cacfe9eULL}) {
        REQUIRE(FindOcarinaUpscalePass(0, hash) == ThreeQuarter);
    }
    for (const u64 hash : {0x6bd913e1f34e83ebULL, 0x9302bc0d6d1d9bc1ULL, 0x4c1ae2fe58f30a97ULL}) {
        REQUIRE(FindOcarinaUpscalePass(0, hash) == Pass1440p);
    }
    REQUIRE(FindOcarinaUpscalePass(0, 0xc033c3ddc9f0897bULL) == FullSize);
    REQUIRE(FindOcarinaUpscalePass(0x1234, 0x5678) == None);
    REQUIRE(IsOcarinaDepthReader(OcarinaDepthReaderKey, 0));
    REQUIRE(IsOcarinaDepthReader(0, 0x1fc7a0423116281fULL));
    REQUIRE_FALSE(IsOcarinaDepthReader(0x1234, 0x5678));
}
