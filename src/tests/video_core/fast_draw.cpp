// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "common/page_table.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/memory.h"
#include "tests/core/system_test_context.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/memory_manager.h"
#include "video_core/nx2.h"
#include "video_core/nx2_fast_draw.h"
#include "video_core/nx2_profile.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_vulkan/fixed_pipeline_state.h"

namespace {

using Tegra::Engines::Maxwell3D;
namespace FastDraw = VideoCommon::Nx2FastDraw;
constexpr u32 ViewportScaleX = MAXWELL3D_REG_INDEX(viewport_transform);

class NoDma final : public Tegra::Engines::AccelerateDMAInterface {
public:
    bool BufferCopy(GPUVAddr, GPUVAddr, u64) override { return true; }
    bool BufferClear(GPUVAddr, u64, u32) override { return true; }
    bool ImageToBuffer(const Tegra::DMA::ImageCopy&, const Tegra::DMA::ImageOperand&,
                       const Tegra::DMA::BufferOperand&) override { return true; }
    bool BufferToImage(const Tegra::DMA::ImageCopy&, const Tegra::DMA::BufferOperand&,
                       const Tegra::DMA::ImageOperand&) override { return true; }
};

/// Records the fast draw serial each draw sees.
class SerialRasterizer final : public VideoCore::RasterizerInterface {
public:
    std::vector<u64> draws;
    std::vector<std::pair<DAddr, std::size_t>> invalidations;

    void InnerInvalidation(std::span<const std::pair<DAddr, std::size_t>> sequences) override {
        invalidations.insert(invalidations.end(), sequences.begin(), sequences.end());
    }

    void Draw(bool, u32) override { draws.push_back(FastDraw::Serial()); }
    void DrawTexture() override {}
    void Clear(u32) override {}
    void DispatchCompute() override {}
    void ResetCounter(VideoCommon::QueryType) override {}
    void Query(GPUVAddr, VideoCommon::QueryType, VideoCommon::QueryPropertiesFlags, u32,
               u32) override {}
    void BindGraphicsUniformBuffer(size_t, u32, GPUVAddr, u32) override {}
    void DisableGraphicsUniformBuffer(size_t, u32) override {}
    void SignalFence(std::function<void()>&& func) override { func(); }
    void SyncOperation(std::function<void()>&& func) override { func(); }
    void SignalSyncPoint(u32) override {}
    void SignalReference() override {}
    void ReleaseFences(bool) override {}
    void FlushAll() override {}
    void FlushRegion(DAddr, u64, VideoCommon::CacheType) override {}
    bool MustFlushRegion(DAddr, u64, VideoCommon::CacheType) override { return false; }
    VideoCore::RasterizerDownloadArea GetFlushArea(DAddr addr, u64 size) override {
        return {addr, addr + size, false};
    }
    void InvalidateRegion(DAddr, u64, VideoCommon::CacheType) override {}
    void OnCacheInvalidation(PAddr, u64) override {}
    bool OnCPUWrite(PAddr, u64) override { return false; }
    void InvalidateGPUCache() override {}
    void UnmapMemory(DAddr, u64) override {}
    void ModifyGPUMemory(size_t, GPUVAddr, u64) override {}
    void FlushAndInvalidateRegion(DAddr, u64, VideoCommon::CacheType) override {}
    void WaitForIdle() override {}
    void FragmentBarrier() override {}
    void TiledCacheBarrier() override {}
    void FlushCommands() override {}
    void TickFrame() override {}
    Tegra::Engines::AccelerateDMAInterface& AccessAccelerateDMA() override { return dma; }
    void AccelerateInlineToMemory(GPUVAddr, size_t, std::span<const u8>) override {}

private:
    NoDma dma;
};

struct FastDrawFixture {
    FastDrawFixture() : memory{system, device_memory, 32, 1ULL << 31}, maxwell3d{memory} {
        SetSwitch(true);
        FastDraw::g_vertex_streams_dynamic = false;
        FastDraw::g_delta = {};
        maxwell3d.BindRasterizer(&rasterizer);
    }
    ~FastDrawFixture() {
        FastDraw::SetEnabledForTest(previous);
        FastDraw::g_vertex_streams_dynamic = previous_vertex_streams_dynamic;
        FastDraw::g_delta = {};
    }

    static void SetSwitch(bool on) {
        FastDraw::SetEnabledForTest(on);
    }

    u32 Reg(u32 method) const {
        return maxwell3d.regs.reg_array[method];
    }

    void Write(u32 method, u32 value) {
        maxwell3d.CallMethod(system, method, value, true);
    }

    void DrawArrays(u32 first, u32 count) {
        Write(MAXWELL3D_REG_INDEX(vertex_buffer.first), first);
        Write(MAXWELL3D_REG_INDEX(vertex_buffer.count), count);
        Write(MAXWELL3D_REG_INDEX(draw.begin), 0x4); // first instance, triangles
        Write(MAXWELL3D_REG_INDEX(draw.end), 0);
    }

    /// Whether the last two draws saw the same serial (the second could reuse the first).
    bool Reusable() const {
        const auto& draws = rasterizer.draws;
        REQUIRE(draws.size() >= 2);
        return draws[draws.size() - 1] == draws[draws.size() - 2];
    }

    const bool previous = FastDraw::Enabled();
    const bool previous_vertex_streams_dynamic = FastDraw::g_vertex_streams_dynamic;
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    Tegra::MemoryManager memory;
    SerialRasterizer rasterizer;
    Maxwell3D maxwell3d;
};

} // namespace

TEST_CASE("Fast draw keeps the serial across draws that change only draw parameters",
          "[fast_draw]") {
    FastDrawFixture f;
    f.DrawArrays(0, 3);
    f.DrawArrays(3, 6);
    REQUIRE(f.rasterizer.draws.size() == 2);
    CHECK(f.Reusable());

    f.Write(ViewportScaleX, f.Reg(ViewportScaleX));
    f.DrawArrays(9, 3);
    CHECK(f.Reusable());

    f.Write(MAXWELL3D_REG_INDEX(const_buffer.offset), 0x40);
    f.Write(MAXWELL3D_REG_INDEX(const_buffer.address_low), 0x1000);
    f.DrawArrays(12, 3);
    CHECK(f.Reusable());
}

TEST_CASE("Fast draw breaks reuse on anything that changes what a draw binds", "[fast_draw]") {
    FastDrawFixture f;
    const auto breaks = [&](auto&& change) {
        f.DrawArrays(0, 3);
        change();
        f.DrawArrays(0, 3);
        return !f.Reusable();
    };
    constexpr u32 blend = MAXWELL3D_REG_INDEX(blend.enable[0]);
    CHECK(breaks([&] { f.Write(blend, f.Reg(blend) ^ 1); }));
    CHECK(breaks([&] {
        f.Write(ViewportScaleX,
                f.Reg(ViewportScaleX) + 1);
    }));
    CHECK(breaks([&] { f.Write(MAXWELL3D_REG_INDEX(clear_surface), 0x3c); }));
    CHECK(breaks([&] { f.Write(MAXWELL3D_REG_INDEX(tiled_cache_barrier), 0); }));
    CHECK(breaks([&] { FastDraw::Break(FastDraw::Reason::Fence); }));
}

TEST_CASE("Fast draw keeps static vertex state conservative", "[fast_draw]") {
    FastDrawFixture f;
    f.DrawArrays(0, 3);
    constexpr u32 stream = MAXWELL3D_REG_INDEX(vertex_streams);
    f.Write(stream, f.Reg(stream) ^ 1); // stride/enable word
    f.DrawArrays(0, 3);
    CHECK_FALSE(f.Reusable());
    constexpr u32 format = MAXWELL3D_REG_INDEX(vertex_attrib_format);
    f.Write(format, f.Reg(format) ^ 1);
    f.DrawArrays(0, 3);
    CHECK_FALSE(f.Reusable());
}

TEST_CASE("Dynamic vertex formats preserve draw reuse only within shader input type classes",
          "[fast_draw]") {
    FastDrawFixture f;
    FastDraw::g_vertex_streams_dynamic = true;
    using Attribute = Maxwell3D::Regs::VertexAttribute;
    using State = Vulkan::FixedPipelineState;
    constexpr u32 format = MAXWELL3D_REG_INDEX(vertex_attrib_format);
    Attribute attribute{};
    attribute.constant.Assign(0U);
    attribute.type.Assign(Attribute::Type::Float);
    attribute.size.Assign(Attribute::Size::Size_R32);
    f.Write(format, attribute.hex);
    f.DrawArrays(0, 3);
    const u64 types = State::DynamicAttributeTypes(f.maxwell3d.regs);

    constexpr u32 stream = MAXWELL3D_REG_INDEX(vertex_streams);
    f.Write(stream, f.Reg(stream) ^ 1);
    attribute.type.Assign(Attribute::Type::UNorm);
    attribute.size.Assign(Attribute::Size::Size_R8_G8_B8_A8);
    attribute.offset.Assign(16U);
    f.Write(format, attribute.hex);
    f.DrawArrays(0, 3);
    CHECK(f.Reusable());
    CHECK(State::CanReuseDynamicVertexInput(f.maxwell3d.regs, true, types));
    CHECK_FALSE(State::CanReuseDynamicVertexInput(f.maxwell3d.regs, false, types));

    // The engine keeps this write light; the rasterizer must reject its changed shader input.
    attribute.type.Assign(Attribute::Type::UInt);
    f.Write(format, attribute.hex);
    f.DrawArrays(0, 3);
    CHECK(f.Reusable());
    CHECK_FALSE(State::CanReuseDynamicVertexInput(f.maxwell3d.regs, true, types));
    attribute.type.Assign(Attribute::Type::SInt);
    f.Write(format, attribute.hex);
    CHECK_FALSE(State::CanReuseDynamicVertexInput(f.maxwell3d.regs, true, types));
    attribute.constant.Assign(1U);
    f.Write(format, attribute.hex);
    CHECK_FALSE(State::CanReuseDynamicVertexInput(f.maxwell3d.regs, true, types));
}

TEST_CASE("Dynamic vertex validation uses the raw draw snapshot before native key masking",
          "[fast_draw]") {
    FastDrawFixture f;
    using Attribute = Maxwell3D::Regs::VertexAttribute;
    using State = Vulkan::FixedPipelineState;
    auto& attribute = f.maxwell3d.regs.vertex_attrib_format[17];
    attribute.constant.Assign(0U);
    attribute.type.Assign(Attribute::Type::Float);
    const u64 raw_types = State::DynamicAttributeTypes(f.maxwell3d.regs);
    State canonical{};
    canonical.dynamic_vertex_input.Assign(1U);
    canonical.attribute_types = raw_types;
    canonical.CanonicalizeNative(1); // Native header does not list attribute 17.
    REQUIRE(canonical.attribute_types != raw_types);
    CHECK(State::CanReuseDynamicVertexInput(f.maxwell3d.regs, true, raw_types));
    attribute.offset.Assign(attribute.offset.Value() + 4U);
    CHECK(State::CanReuseDynamicVertexInput(f.maxwell3d.regs, true, raw_types));
    attribute.type.Assign(Attribute::Type::UInt);
    CHECK_FALSE(State::CanReuseDynamicVertexInput(f.maxwell3d.regs, true, raw_types));
}

TEST_CASE("Fast draw overflow retains every GPU and device write conservatively",
          "[fast_draw]") {
    FastDrawFixture f;
    const u64 serial = FastDraw::Serial();
    for (u32 i = 0; i < 4 * FastDraw::Delta::MaxWrites; ++i) {
        FastDraw::NoteConstantData(0x60000 + i * 0x20, 0x60004 + i * 0x20);
        FastDraw::NoteDeviceData(0xA0000 + i * 0x40, 0xA0004 + i * 0x40);
    }
    CHECK(FastDraw::Serial() == serial);
    const auto& delta = FastDraw::g_delta;
    REQUIRE(delta.num_writes == FastDraw::Delta::MaxWrites);
    REQUIRE(delta.num_device_writes == FastDraw::Delta::MaxWrites);
    REQUIRE(delta.no_uniform_keeps);
    const auto covered = [](const auto& begins, const auto& ends, u32 count, u64 address) {
        for (u32 i = 0; i < count; ++i) {
            if (begins[i] <= address && address + 4 <= ends[i]) {
                return true;
            }
        }
        return false;
    };
    for (u32 i = 0; i < 4 * FastDraw::Delta::MaxWrites; ++i) {
        CHECK(covered(delta.write_begin, delta.write_end, delta.num_writes, 0x60000 + i * 0x20));
        CHECK(covered(delta.device_begin, delta.device_end, delta.num_device_writes,
                      0xA0000 + i * 0x40));
    }
}

TEST_CASE("Fast draw serial never moves while disabled", "[fast_draw]") {
    FastDrawFixture f;
    FastDrawFixture::SetSwitch(false);
    f.DrawArrays(0, 3);
    f.Write(MAXWELL3D_REG_INDEX(blend.enable[0]), f.Reg(MAXWELL3D_REG_INDEX(blend.enable[0])) ^ 1);
    f.Write(MAXWELL3D_REG_INDEX(const_buffer.buffer), 1);
    f.DrawArrays(0, 3);
    CHECK(f.Reusable());
}

namespace {

struct DeltaFixture : FastDrawFixture {
    void SetConstantBuffer(GPUVAddr address, u32 size, u32 offset) {
        Write(MAXWELL3D_REG_INDEX(const_buffer.size), size);
        Write(MAXWELL3D_REG_INDEX(const_buffer.address_high), static_cast<u32>(address >> 32));
        Write(MAXWELL3D_REG_INDEX(const_buffer.address_low), static_cast<u32>(address));
        Write(MAXWELL3D_REG_INDEX(const_buffer.offset), offset);
    }
};

/// Production engine and memory manager, with real mapped guest bytes and observable cached
/// write invalidations. No renderer or guest game is launched.
struct ConstantDiagnosticFixture : DeltaFixture {
    static constexpr GPUVAddr Gpu = 0x800000;
    static constexpr DAddr Device = 0x10000;
    static constexpr VAddr Cpu = 0x4000;

    ConstantDiagnosticFixture() {
        system.Initialize();
        process = std::make_unique<Kernel::KProcess>(system.Kernel());
        auto& table = process->GetPageTable().GetImpl();
        table.Resize(20, 12);
        backing = physical.GetPointerFromRaw<u8>(Device);
        std::memset(backing, 0, 0x2000);
        for (u64 page = 0; page < 2; ++page) {
            const VAddr address = Cpu + page * 0x1000;
            table.entries.GetAndFault(address >> 12)
                .Store(false, Common::PageType::Memory, 1,
                       reinterpret_cast<uintptr_t>(backing + page * 0x1000) - address);
        }
        process->GetMemory().SetCurrentPageTable(*process);
        asid = device_memory.RegisterProcess(&process->GetMemory());
        device_memory.BindInterface(&rasterizer);
        device_memory.Map(Device, Cpu, 0x2000, asid);
        memory.BindRasterizer(&rasterizer);
        memory.Map(Gpu, Device, 0x2000, Tegra::PTEKind::PITCH, false);
        maxwell3d.fast_maxwell3d = true;
    }
    ~ConstantDiagnosticFixture() {
        device_memory.Unmap(Device, 0x2000);
        device_memory.UnregisterProcess(asid);
        system.Kernel().UnregisterKernelObject(process.get());
    }
    bool Invalidated(DAddr begin, u64 bytes) const {
        return std::ranges::any_of(rasterizer.invalidations, [&](const auto& span) {
            return span.first <= begin && begin + bytes <= span.first + span.second;
        });
    }

    std::unique_ptr<Kernel::KProcess> process;
    Core::Asid asid{};
    u8* backing{};
};

struct ScopedConstantDiagnostics {
    explicit ScopedConstantDiagnostics(bool enabled)
        : profile{VideoCommon::Nx2Profile::g_enabled},
          ampere{Tegra::g_nx2_ampere.load()}, sparse_low{Tegra::g_nx2_sparse_low.load()},
          sparse_high{Tegra::g_nx2_sparse_high.load()},
          written{Tegra::g_nx2_gpu_written_pages[0].load()},
          counts{FastDraw::t_constant_write_diagnostics} {
        VideoCommon::Nx2Profile::g_enabled = enabled;
        Tegra::g_nx2_ampere = true;
        Tegra::g_nx2_sparse_low = ~u64{0};
        Tegra::g_nx2_sparse_high = 0;
        Tegra::g_nx2_gpu_written_pages[0] = 0;
        FastDraw::t_constant_write_diagnostics = {};
    }
    ~ScopedConstantDiagnostics() {
        VideoCommon::Nx2Profile::g_enabled = profile;
        Tegra::g_nx2_ampere = ampere;
        Tegra::g_nx2_sparse_low = sparse_low;
        Tegra::g_nx2_sparse_high = sparse_high;
        Tegra::g_nx2_gpu_written_pages[0] = written;
        FastDraw::t_constant_write_diagnostics = counts;
    }
    bool profile, ampere;
    u64 sparse_low, sparse_high, written;
    FastDraw::ConstantWriteDiagnostics counts;
};

} // namespace

TEST_CASE("CB diagnostics preserve equal and changed engine writes and invalidations",
          "[fast_draw][cb_diagnostics]") {
    const bool diagnose = GENERATE(false, true);
    const bool word_batch = GENERATE(false, true);
    if (!diagnose && FastDraw::VerifyEnabled()) {
        SKIP("The process-wide verification environment keeps diagnostics enabled");
    }
    ConstantDiagnosticFixture f;
    const ScopedConstantDiagnostics diagnostics{diagnose};
    constexpr u32 offset = 0x40;
    constexpr u32 cb_data = MAXWELL3D_REG_INDEX(const_buffer.buffer);
    const std::array<u32, 2> methods{cb_data, cb_data + 1};
    const std::array<u32, 2> original{0x11223344, 0x55667788};
    const std::array<u32, 2> changed{0xabcdef01, 0x23456789};
    std::memcpy(f.backing + offset, original.data(), sizeof(original));
    f.SetConstantBuffer(f.Gpu, 0x2000, offset);
    const auto write = [&](const auto& values) {
        if (word_batch) {
            f.maxwell3d.CallConstBufferWords(f.system, methods.data(), values.data(), 2);
        } else {
            f.maxwell3d.CallMultiMethod(f.system, cb_data, values.data(), 2, 2);
        }
    };
    const u64 serial = FastDraw::Serial();
    FastDraw::g_delta = {};
    write(original);
    CHECK(std::memcmp(f.backing + offset, original.data(), sizeof(original)) == 0);
    CHECK(f.Reg(MAXWELL3D_REG_INDEX(const_buffer.offset)) == offset + sizeof(original));
    CHECK(FastDraw::Serial() == serial);
    REQUIRE(FastDraw::g_delta.num_writes == 1);
    CHECK(FastDraw::g_delta.write_begin[0] == f.Gpu + offset);
    CHECK(FastDraw::g_delta.write_end[0] == f.Gpu + offset + sizeof(original));
    REQUIRE(FastDraw::g_delta.num_device_writes == 1);
    CHECK(FastDraw::g_delta.device_begin[0] == f.Device + offset);
    CHECK(FastDraw::g_delta.device_end[0] == f.Device + offset + sizeof(original));
    CHECK(FastDraw::g_delta.diagnostic_has_data);
    CHECK(FastDraw::g_delta.diagnostic_all_known_equal == diagnose);
    CHECK(FastDraw::t_constant_write_diagnostics.writes == (diagnose ? 1 : 0));
    CHECK(FastDraw::t_constant_write_diagnostics.equal_writes == (diagnose ? 1 : 0));
    CHECK(FastDraw::t_constant_write_diagnostics.equal_bytes == (diagnose ? sizeof(original) : 0));
    f.memory.FlushCaching();
    CHECK(f.Invalidated(f.Device + offset, sizeof(original)));

    f.rasterizer.invalidations.clear();
    f.SetConstantBuffer(f.Gpu, 0x2000, offset);
    FastDraw::g_delta = {};
    write(changed);
    CHECK(std::memcmp(f.backing + offset, changed.data(), sizeof(changed)) == 0);
    CHECK(f.Reg(MAXWELL3D_REG_INDEX(const_buffer.offset)) == offset + sizeof(changed));
    CHECK(FastDraw::Serial() == serial);
    CHECK(FastDraw::g_delta.num_writes == 1);
    CHECK(FastDraw::g_delta.num_device_writes == 1);
    CHECK_FALSE(FastDraw::g_delta.diagnostic_all_known_equal);
    CHECK(FastDraw::t_constant_write_diagnostics.writes == (diagnose ? 2 : 0));
    CHECK(FastDraw::t_constant_write_diagnostics.changed_writes == (diagnose ? 1 : 0));
    CHECK(FastDraw::t_constant_write_diagnostics.changed_bytes == (diagnose ? sizeof(changed) : 0));
    f.memory.FlushCaching();
    CHECK(f.Invalidated(f.Device + offset, sizeof(changed)));
}

TEST_CASE("CB diagnostics classify unsupported writes without changing engine effects",
          "[fast_draw][cb_diagnostics]") {
    // Unmapped, cross-page, sparse, ever GPU-written, non-native, pending report.
    const u32 kind = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(kind);
    ConstantDiagnosticFixture f;
    const ScopedConstantDiagnostics diagnostics{true};
    const GPUVAddr address = kind == 0 ? f.Gpu + 0x4000 : f.Gpu;
    const u32 offset = kind == 1 ? 0xffc : 0x40;
    const std::array<u32, 2> words{0x12345678, 0xabcdef01};
    f.SetConstantBuffer(address, 0x2000, offset);
    if (kind == 2) {
        Tegra::g_nx2_sparse_low = address + offset;
        Tegra::g_nx2_sparse_high = address + offset + sizeof(words);
    }
    if (kind == 3) {
        Tegra::Nx2MarkGpuWritten(f.Device + offset, sizeof(words));
    }
    if (kind == 4) {
        Tegra::g_nx2_ampere = false;
    }
    std::optional<Tegra::Nx2AsyncReport> report;
    if (kind == 5) {
        // A 16-byte report beginning in the preceding slot can overlap this CB range.
        report.emplace(f.Device + offset - 8);
        report->Issue();
    }
    FastDraw::g_delta = {};
    const u64 serial = FastDraw::Serial();
    f.maxwell3d.CallMultiMethod(f.system, MAXWELL3D_REG_INDEX(const_buffer.buffer),
                               words.data(), 2, 2);
    const auto& counts = FastDraw::t_constant_write_diagnostics;
    CHECK(counts.writes == 1);
    CHECK(counts.bytes == sizeof(words));
    CHECK(counts.unavailable == (kind == 0 ? 1 : 0));
    CHECK(counts.cross_page == (kind == 1 ? 1 : 0));
    CHECK(counts.sparse == (kind == 2 ? 1 : 0));
    CHECK(counts.may_gpu_written == (kind == 3 ? 1 : 0));
    CHECK(counts.non_native == (kind == 4 ? 1 : 0));
    CHECK(counts.pending_report == (kind == 5 ? 1 : 0));
    CHECK(counts.equal_writes == 0);
    CHECK(counts.changed_writes == 0);
    CHECK(FastDraw::Serial() == serial);
    CHECK_FALSE(FastDraw::g_delta.diagnostic_all_known_equal);
    CHECK_FALSE(FastDraw::g_delta.Empty());
    CHECK(f.Reg(MAXWELL3D_REG_INDEX(const_buffer.offset)) == offset + sizeof(words));
    if (kind != 0) {
        CHECK(std::memcmp(f.backing + offset, words.data(), sizeof(words)) == 0);
        f.memory.FlushCaching();
        CHECK(f.Invalidated(f.Device + offset, sizeof(words)));
    }
}

TEST_CASE("Equal CB diagnostics retain direct and aliased header serial breaks",
          "[fast_draw][cb_diagnostics]") {
    const bool alias = GENERATE(false, true);
    ConstantDiagnosticFixture f;
    const ScopedConstantDiagnostics diagnostics{true};
    constexpr GPUVAddr alias_address = f.Gpu + 0x100000;
    if (alias) {
        f.memory.Map(alias_address, f.Device, 0x1000, Tegra::PTEKind::PITCH, false);
    }
    f.Write(MAXWELL3D_REG_INDEX(tex_header.address_high), 0);
    f.Write(MAXWELL3D_REG_INDEX(tex_header.address_low), static_cast<u32>(f.Gpu));
    f.Write(MAXWELL3D_REG_INDEX(tex_header.limit), 7);
    f.SetConstantBuffer(alias ? alias_address : f.Gpu, 0x1000, 0x20);
    const std::array<u32, 2> equal_words{};
    FastDraw::g_delta = {};
    const u64 serial = FastDraw::Serial();
    f.maxwell3d.CallMultiMethod(f.system, MAXWELL3D_REG_INDEX(const_buffer.buffer),
                               equal_words.data(), 2, 2);
    CHECK(FastDraw::t_constant_write_diagnostics.equal_writes == 1);
    CHECK(FastDraw::Serial() != serial);
    CHECK(FastDraw::g_delta.num_writes == (alias ? 1 : 0));
    CHECK(std::memcmp(f.backing + 0x20, equal_words.data(), sizeof(equal_words)) == 0);
    f.memory.FlushCaching();
    CHECK(f.Invalidated(f.Device + 0x20, sizeof(equal_words)));
}

TEST_CASE("Unknown report stores clear only the CB diagnostic equality marker",
          "[fast_draw][cb_diagnostics]") {
    ConstantDiagnosticFixture f;
    const ScopedConstantDiagnostics diagnostics{true};
    f.SetConstantBuffer(f.Gpu, 0x1000, 0x40);
    FastDraw::g_delta = {};
    f.Write(MAXWELL3D_REG_INDEX(const_buffer.buffer), 0);
    REQUIRE(FastDraw::g_delta.diagnostic_all_known_equal);
    const u64 serial = FastDraw::Serial();
    f.Write(MAXWELL3D_REG_INDEX(report_semaphore.address_high), 0);
    f.Write(MAXWELL3D_REG_INDEX(report_semaphore.address_low), static_cast<u32>(f.Gpu + 0x100));
    f.Write(MAXWELL3D_REG_INDEX(report_semaphore.payload), 1);
    f.Write(MAXWELL3D_REG_INDEX(report_semaphore.query), 0);
    CHECK(FastDraw::Serial() == serial);
    CHECK_FALSE(FastDraw::g_delta.diagnostic_all_known_equal);
    CHECK(FastDraw::t_constant_write_diagnostics.writes == 1);
    REQUIRE(FastDraw::g_delta.num_writes == 2);
    CHECK(FastDraw::g_delta.write_begin[1] == f.Gpu + 0x100);
    CHECK(FastDraw::g_delta.write_end[1] == f.Gpu + 0x110);
    FastDraw::DeviceSpans spans;
    spans.Update(f.memory, f.Gpu + 0x100, 0x10);
    CHECK(FastDraw::g_delta.Reaches(f.Gpu + 0x100, 0x10, spans));
}

TEST_CASE("Fast draw records bind and constant data changes, keeps index buffer changes light",
          "[fast_draw]") {
    DeltaFixture f;
    f.DrawArrays(0, 3);
    const u64 serial = FastDraw::Serial();
    FastDraw::g_delta = {};

    f.Write(MAXWELL3D_REG_INDEX(index_buffer.start_addr_low),
            f.Reg(MAXWELL3D_REG_INDEX(index_buffer.start_addr_low)) + 0x100);
    f.Write(MAXWELL3D_REG_INDEX(index_buffer.format), f.Reg(MAXWELL3D_REG_INDEX(index_buffer.format)) ^ 1);
    CHECK(FastDraw::Serial() == serial);
    CHECK(FastDraw::g_delta.Empty());

    f.SetConstantBuffer(0x10000, 0x100, 0);
    f.Write(MAXWELL3D_REG_INDEX(bind_groups[4].raw_config), 0x31); // slot 3, valid
    CHECK(FastDraw::g_delta.binds[4] == 1U << 3);
    FastDraw::g_delta.binds[4] = 0;
    f.Write(MAXWELL3D_REG_INDEX(bind_groups[4].raw_config), 0x31);
    CHECK(FastDraw::g_delta.binds[4] == 0);

    f.SetConstantBuffer(0x20000, 0x100, 0x10);
    f.Write(MAXWELL3D_REG_INDEX(const_buffer.buffer), 1);
    f.Write(MAXWELL3D_REG_INDEX(const_buffer.buffer) + 1, 2);
    REQUIRE(FastDraw::g_delta.num_writes == 1);
    CHECK(FastDraw::g_delta.write_begin[0] == 0x20010);
    CHECK(FastDraw::g_delta.write_end[0] == 0x20018);

    CHECK(FastDraw::Serial() == serial);
    f.DrawArrays(0, 3);
    CHECK(f.Reusable());
    CHECK_FALSE(FastDraw::g_delta.Empty());
}

TEST_CASE("Fast draw breaks on state and texture headers", "[fast_draw]") {
    DeltaFixture f;
    f.Write(MAXWELL3D_REG_INDEX(tex_header.address_high), 0);
    f.Write(MAXWELL3D_REG_INDEX(tex_header.address_low), 0x40000);
    f.Write(MAXWELL3D_REG_INDEX(tex_header.limit), 7);
    f.Write(MAXWELL3D_REG_INDEX(tex_sampler.address_high), 0);
    f.Write(MAXWELL3D_REG_INDEX(tex_sampler.address_low), 0x50000);
    f.Write(MAXWELL3D_REG_INDEX(tex_sampler.limit), 7);
    const auto breaks = [&](auto&& change) {
        FastDraw::g_delta = {};
        const u64 serial = FastDraw::Serial();
        change();
        return FastDraw::Serial() != serial;
    };
    constexpr u32 blend = MAXWELL3D_REG_INDEX(blend.enable[0]);
    CHECK(breaks([&] { f.Write(blend, f.Reg(blend) ^ 1); }));
    CHECK(breaks([&] {
        f.SetConstantBuffer(0x40000, 0x1000, 0xe0); // the last texture header
        f.Write(MAXWELL3D_REG_INDEX(const_buffer.buffer), 1);
    }));
    CHECK(breaks([&] {
        f.SetConstantBuffer(0x50000, 0x1000, 0);
        f.Write(MAXWELL3D_REG_INDEX(const_buffer.buffer), 1);
    }));
    CHECK_FALSE(breaks([&] {
        f.SetConstantBuffer(0x40000, 0x1000, 0x100); // just past the headers
        f.Write(MAXWELL3D_REG_INDEX(const_buffer.buffer), 1);
    }));
    CHECK_FALSE(breaks([&] {
        f.SetConstantBuffer(0x60000, 0x1000, 0);
        for (u32 i = 0; i <= FastDraw::Delta::MaxWrites; ++i) {
            f.Write(MAXWELL3D_REG_INDEX(const_buffer.offset), i * 0x20);
            f.Write(MAXWELL3D_REG_INDEX(const_buffer.buffer), i);
        }
    }));
    CHECK(FastDraw::g_delta.no_uniform_keeps);
    REQUIRE(FastDraw::g_delta.num_writes == FastDraw::Delta::MaxWrites);
    for (u32 i = 0; i <= FastDraw::Delta::MaxWrites; ++i) {
        const GPUVAddr address = 0x60000 + i * 0x20;
        bool covered = false;
        for (u32 write = 0; write < FastDraw::g_delta.num_writes; ++write) {
            covered |= FastDraw::g_delta.write_begin[write] <= address &&
                       address + 4 <= FastDraw::g_delta.write_end[write];
        }
        CHECK(covered);
    }
}

TEST_CASE("Fast draw breaks on texture header data written through another mapping",
          "[fast_draw]") {
    DeltaFixture f;
    f.memory.BindRasterizer(&f.rasterizer);
    constexpr GPUVAddr headers = 0x40000;
    constexpr GPUVAddr alias = 0x90000;
    f.memory.Map(headers, 0x100000, 0x1000, Tegra::PTEKind::PITCH, false);
    f.memory.Map(alias, 0x100000, 0x1000, Tegra::PTEKind::PITCH, false);
    f.Write(MAXWELL3D_REG_INDEX(tex_header.address_high), 0);
    f.Write(MAXWELL3D_REG_INDEX(tex_header.address_low), static_cast<u32>(headers));
    f.Write(MAXWELL3D_REG_INDEX(tex_header.limit), 7);
    const auto breaks = [&](u32 offset) {
        FastDraw::g_delta = {};
        const u64 serial = FastDraw::Serial();
        f.SetConstantBuffer(alias, 0x1000, offset);
        f.Write(MAXWELL3D_REG_INDEX(const_buffer.buffer), 1);
        return FastDraw::Serial() != serial;
    };
    CHECK(breaks(0xe0));        // the last texture header
    CHECK_FALSE(breaks(0x100)); // just past the headers
}

TEST_CASE("Fast draw finds texture handle data written through another mapping",
          "[fast_draw]") {
    DeltaFixture f;
    f.memory.BindRasterizer(&f.rasterizer);
    constexpr GPUVAddr handles = 0x100000; // a buffer of texture handles, 0x100 bytes
    constexpr GPUVAddr alias = 0x300000;   // its device memory, mapped again
    constexpr GPUVAddr other = 0x500000;   // other device memory
    constexpr GPUVAddr unmapped = 0x700000;
    constexpr auto Kind = Tegra::PTEKind::PITCH;
    f.memory.Map(handles, 0x200000, 0x1000, Kind, false);
    f.memory.Map(alias, 0x200000, 0x1000, Kind, false);
    f.memory.Map(other, 0x400000, 0x1000, Kind, false);
    FastDraw::DeviceSpans spans;
    const auto reaches = [&](GPUVAddr handle_buffer, GPUVAddr buffer, u32 offset,
                             std::vector<u32> words) {
        FastDraw::g_delta = {};
        f.SetConstantBuffer(buffer, 0x2000, offset);
        f.maxwell3d.CallMultiMethod(f.system, MAXWELL3D_REG_INDEX(const_buffer.buffer),
                                    words.data(), static_cast<u32>(words.size()),
                                    static_cast<u32>(words.size()));
        spans.Update(f.memory, handle_buffer, 0x100);
        return FastDraw::g_delta.Reaches(handle_buffer, 0x100, spans);
    };
    CHECK(reaches(handles, handles, 0x10, {1}));
    CHECK(reaches(handles, alias, 0x10, {1})); // missed when compared by GPU address only
    CHECK(reaches(handles, alias, 0xfc, {1, 2}));
    CHECK_FALSE(reaches(handles, alias, 0x100, {1})); // just past the handles
    CHECK_FALSE(reaches(handles, other, 0x10, {1}));
    // Nothing mapped there: compared by GPU address, as before.
    CHECK(reaches(unmapped, unmapped, 0x10, {1}));
    CHECK_FALSE(reaches(unmapped, other, 0x10, {1}));

    // A write across two pages that map device memory apart lands on both.
    f.memory.Map(alias, 0x201000, 0x1000, Kind, false);
    f.memory.Map(alias + 0x1000, 0x200000, 0x1000, Kind, false);
    CHECK(reaches(handles, alias, 0xffc, {1, 2}));
    CHECK(FastDraw::g_delta.num_device_writes == 2);
    CHECK_FALSE(reaches(handles, alias, 0xff8, {1, 2}));

    // Mapping changes translate the handle buffer again.
    CHECK_FALSE(reaches(handles, other, 0x10, {1}));
    f.memory.Map(handles, 0x400000, 0x1000, Kind, false);
    CHECK(reaches(handles, other, 0x10, {1}));
    CHECK_FALSE(reaches(handles, alias + 0x1000, 0x10, {1}));
}

TEST_CASE("Fast draw records and breaks nothing while switched off", "[fast_draw]") {
    DeltaFixture f;
    FastDrawFixture::SetSwitch(false);
    const u64 serial = FastDraw::Serial();
    f.Write(MAXWELL3D_REG_INDEX(blend.enable[0]), f.Reg(MAXWELL3D_REG_INDEX(blend.enable[0])) ^ 1);
    f.SetConstantBuffer(0x10000, 0x100, 0);
    f.Write(MAXWELL3D_REG_INDEX(bind_groups[0].raw_config), 0x11);
    f.Write(MAXWELL3D_REG_INDEX(const_buffer.buffer), 1);
    FastDraw::Break(FastDraw::Reason::Fence);
    CHECK(FastDraw::Serial() == serial);
    CHECK(FastDraw::g_delta.Empty());
}

TEST_CASE("Fast draw keeps base vertex and instance as draw parameters", "[fast_draw]") {
    FastDrawFixture f;
    f.DrawArrays(0, 3);
    f.Write(MAXWELL3D_REG_INDEX(global_base_vertex_index),
            f.Reg(MAXWELL3D_REG_INDEX(global_base_vertex_index)) + 64);
    f.Write(MAXWELL3D_REG_INDEX(global_base_instance_index),
            f.Reg(MAXWELL3D_REG_INDEX(global_base_instance_index)) + 1);
    f.DrawArrays(0, 3);
    CHECK(f.Reusable());
}

TEST_CASE("Fast draw keeps counter resets and the render enable condition light", "[fast_draw]") {
    FastDrawFixture f;
    f.DrawArrays(0, 3);
    constexpr u32 clear_report = MAXWELL3D_REG_INDEX(clear_report_value);
    f.Write(clear_report, f.Reg(clear_report) ^ 1);
    f.DrawArrays(3, 3);
    CHECK(f.Reusable());
    f.Write(MAXWELL3D_REG_INDEX(render_enable.mode), 1); // True: the next draws run
    f.DrawArrays(6, 3);
    CHECK(f.rasterizer.draws.size() == 3);
    CHECK(f.Reusable());
}

TEST_CASE("Fast draw records a report's memory write instead of breaking on it", "[fast_draw]") {
    DeltaFixture f;
    f.Write(MAXWELL3D_REG_INDEX(tex_header.address_high), 0);
    f.Write(MAXWELL3D_REG_INDEX(tex_header.address_low), 0x40000);
    f.Write(MAXWELL3D_REG_INDEX(tex_header.limit), 7);
    const auto report = [&](u32 address) {
        f.Write(MAXWELL3D_REG_INDEX(report_semaphore.address_high), 0);
        f.Write(MAXWELL3D_REG_INDEX(report_semaphore.address_low), address);
        f.Write(MAXWELL3D_REG_INDEX(report_semaphore.payload),
                f.Reg(MAXWELL3D_REG_INDEX(report_semaphore.payload)) + 1);
        f.Write(MAXWELL3D_REG_INDEX(report_semaphore.query), 0); // long release
    };
    f.DrawArrays(0, 3);
    const u64 serial = FastDraw::Serial();
    FastDraw::g_delta = {};
    f.Write(0x155, f.Reg(0x155) + 0x40); // SET_MME_MEM_ADDRESS_B
    report(0x30000);
    CHECK(FastDraw::Serial() == serial);
    REQUIRE(FastDraw::g_delta.num_writes == 1);
    CHECK(FastDraw::g_delta.write_begin[0] == 0x30000);
    CHECK(FastDraw::g_delta.write_end[0] == 0x30010);
    f.DrawArrays(3, 3);
    CHECK(f.Reusable());

    // Into the texture headers a delta draw keeps: a full resolve.
    report(0x40020);
    CHECK(FastDraw::Serial() != serial);
}

TEST_CASE("Fast draw keeps the vertex id base as a draw parameter", "[fast_draw]") {
    FastDrawFixture f;
    f.DrawArrays(0, 3);
    f.Write(MAXWELL3D_REG_INDEX(vertex_id_base), f.Reg(MAXWELL3D_REG_INDEX(vertex_id_base)) + 3);
    f.DrawArrays(3, 3);
    CHECK(f.Reusable());
}

TEST_CASE("Fast draw keeps vertex stream addresses and sizes light", "[fast_draw]") {
    DeltaFixture f;
    const auto stream = [](u32 index, u32 word) {
        return static_cast<u32>(MAXWELL3D_REG_INDEX(vertex_streams) + index * 4 + word);
    };
    const auto light = [&](u32 method) {
        FastDraw::g_delta = {};
        const u64 serial = FastDraw::Serial();
        f.Write(method, f.Reg(method) + 0x40);
        return FastDraw::Serial() == serial;
    };
    CHECK(light(stream(0, 1)));
    CHECK(light(stream(3, 2)));
    CHECK(light(0x600 / 4 + 1));          // SET_VERTEX_STREAM_SIZE_B(0)
    CHECK(light(0x600 / 4 + 31 * 2));     // SET_VERTEX_STREAM_SIZE_A(31)
    CHECK(light(MAXWELL3D_REG_INDEX(vertex_stream_limits) + 5 * 2 + 1));
    CHECK_FALSE(light(stream(0, 0)));     // stride and enable
    CHECK_FALSE(light(stream(0, 3)));     // frequency
    CHECK_FALSE(light(0x600 / 4 + 64));   // past the sizes
}

TEST_CASE("Fast draw keeps vertex attribute formats light only with dynamic vertex input",
          "[fast_draw]") {
    DeltaFixture f;
    constexpr u32 formats = MAXWELL3D_REG_INDEX(vertex_attrib_format);
    const auto light = [&](u32 method) {
        FastDraw::g_delta = {};
        const u64 serial = FastDraw::Serial();
        f.Write(method, f.Reg(method) + 0x40); // the attribute's offset
        return FastDraw::Serial() == serial;
    };
    const bool previous = std::exchange(FastDraw::g_vertex_streams_dynamic, false);
    CHECK_FALSE(light(formats)); // the formats are in the pipeline key
    FastDraw::g_vertex_streams_dynamic = true;
    CHECK(light(formats));
    CHECK(light(formats + Maxwell3D::Regs::NumVertexAttributes - 1));
    CHECK_FALSE(light(formats + Maxwell3D::Regs::NumVertexAttributes));
    f.DrawArrays(0, 3);
    f.Write(formats + 2, f.Reg(formats + 2) + 0x40);
    f.DrawArrays(3, 3);
    CHECK(f.Reusable());
    FastDraw::g_vertex_streams_dynamic = previous;
}

TEST_CASE("Fast draw breaks skip the add only on the drawing thread, after its first since a read",
          "[fast_draw]") {
    FastDraw::SetEnabledForTest(true);
    const auto current = [] { return FastDraw::g_serial.load(std::memory_order_relaxed); };
    const u64 read = FastDraw::Serial();
    FastDraw::Break(FastDraw::Reason::Register, 1);
    CHECK(current() == read + 1);
    // More breaks before the next read change nothing a draw compares.
    FastDraw::Break(FastDraw::Reason::Register, 2);
    FastDraw::Break(FastDraw::Reason::Method, 3);
    CHECK(current() == read + 1);
    // A thread that never reads the serial bumps every time.
    std::thread other{[] {
        FastDraw::Break(FastDraw::Reason::Invalidation);
        FastDraw::Break(FastDraw::Reason::Invalidation);
    }};
    other.join();
    CHECK(current() == read + 3);
    // After a read, the drawing thread's next break bumps again.
    const u64 again = FastDraw::Serial();
    CHECK(again == read + 3);
    FastDraw::Break(FastDraw::Reason::Register, 4);
    CHECK(current() == again + 1);
}
