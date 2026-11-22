// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <memory>
#include <span>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "tests/video_core/null_rasterizer.h"
#include "video_core/texture_cache/texture_cache.h"

namespace {
struct Runtime {
    struct Download {
        std::shared_ptr<std::vector<u8>> bytes;
        std::span<u8> mapped_span;
    };
    u32 failures = 0;
    size_t downloads = 0;
    size_t waits = 0;
    size_t freed = 0;
    bool capped_share = false;
    u64 heap_budget = 0;
    size_t heap_reads = 0;
    u32 FailedImages() const { return failures; }
    bool CapsDeviceMemoryBudget() const { return capped_share; }
    u64 GetDeviceHeapMemoryBudget() {
        ++heap_reads;
        return heap_budget;
    }
    Download DownloadStagingBuffer(size_t size, bool = false) {
        ++downloads;
        auto bytes = std::make_shared<std::vector<u8>>(size);
        return {bytes, *bytes};
    }
    void Finish() { ++waits; }
    void FreeDeferredStagingBuffer(const Download&) { ++freed; }
    void FlushDeferredClear() {}
};
struct Image : VideoCommon::ImageBase {
    using ImageBase::ImageBase;
    template <typename Copies>
    void DownloadMemory(const Runtime::Download&, const Copies&) {}
};
struct ImageView {
    ImageView(Runtime&, VideoCommon::NullImageViewParams) {}
};
struct Sampler {
    Sampler(Runtime&, const Tegra::Texture::TSCEntry&) {}
};
struct Empty {};
struct ImageAlloc {
    std::vector<VideoCommon::ImageId> images;
};
struct Params {
    static constexpr bool ENABLE_VALIDATION = false;
    static constexpr bool FRAMEBUFFER_BLITS = false;
    static constexpr bool HAS_EMULATED_COPIES = false;
    static constexpr bool HAS_DEVICE_MEMORY_INFO = false;
    static constexpr bool IMPLEMENTS_ASYNC_DOWNLOADS = false;
    static constexpr bool HAS_MSAA_DOWNLOADS = false;
    using Runtime = ::Runtime;
    using Image = ::Image;
    using ImageView = ::ImageView;
    using Sampler = ::Sampler;
    using ImageAlloc = ::ImageAlloc;
    using Framebuffer = Empty;
    using AsyncBuffer = Empty;
    using BufferType = u64;
};
using Cache = VideoCommon::TextureCache<Params>;
}

namespace VideoCommon {
struct TextureCacheTestAccess {
    static ImageId Register(Cache& cache, size_t address_space, DAddr cpu, bool sparse,
                            GPUVAddr gpu = 0x100000) {
        const auto id = cache.slot_images.insert(NullImageParams{});
        auto& image = cache.slot_images[id];
        image.address_space = address_space;
        image.gpu_addr = gpu;
        image.cpu_addr = cpu;
        image.guest_size_bytes = 4096;
        image.flags = ImageFlagBits::Registered;
        image.lru_index = cache.lru_cache.Insert(id, 0);
        const auto storage = cache.getStorageID(address_space).value();
        cache.gpu_page_table_storage[storage * 2][gpu >> 20].push_back(id);
        const auto map = cache.slot_map_views.insert(image.gpu_addr, cpu, 4096, id);
        cache.page_table[cpu >> 20].push_back(map);
        if (sparse) {
            image.flags |= ImageFlagBits::Sparse;
            cache.gpu_page_table_storage[storage * 2 + 1][gpu >> 20].push_back(id);
            cache.sparse_views[id].push_back(map);
        } else {
            image.map_view_id = map;
        }
        return id;
    }
    static void AddGcImage(Cache& cache, size_t address_space, size_t index, u64 last_use,
                           ImageFlagBits flags) {
        const auto id = Register(cache, address_space, 0x200000 + (index << 20), false,
                                 0x100000 + (index << 20));
        auto& image = cache.slot_images[id];
        image.flags |= flags;
        image.info.format = PixelFormat::A8B8G8R8_UNORM;
        image.info.type = ImageType::e2D;
        image.info.size = {16, 16, 1};
        image.info.resources = {1, 1};
        image.unswizzled_size_bytes = 1024;
        image.cpu_addr_end = image.cpu_addr + image.guest_size_bytes;
        cache.lru_cache.Touch(image.lru_index, last_use);
        const auto allocation = cache.slot_image_allocs.insert();
        cache.slot_image_allocs[allocation].images.push_back(id);
        cache.image_allocs_table[image.gpu_addr] = allocation;
    }
    static void ConfigureGc(Cache& cache, u64 usage = 60000) {
        cache.frame_tick = 200;
        cache.total_used_memory = usage;
        cache.minimum_memory = 30000;
        cache.expected_memory = 40960;
        cache.critical_memory = 81920;
    }
    static void FrameGc(Cache& cache, bool spill_collect) {
        auto& runtime = cache.runtime;
        auto& seen_failed_images = cache.seen_failed_images;
        auto& total_used_memory = cache.total_used_memory;
        auto& minimum_memory = cache.minimum_memory;
        auto& active_channel_ids = cache.active_channel_ids;
        auto& channel_storage = cache.channel_storage;
        const auto RunGarbageCollector = [&cache](auto... args) {
            cache.RunGarbageCollector(args...);
        };
#include "texture_gc_frame.inc"
    }
    static std::array<u64, 3> BudgetAdjustment(Cache& cache, u64 budget, u64 reserved) {
        auto& runtime = cache.runtime;
        auto& slot_images = cache.slot_images;
        const auto total_used_memory = cache.total_used_memory;
        u64 critical_memory = cache.critical_memory;
        u64 expected_memory = cache.expected_memory;
        const auto ImageAllocationBytes = [&](const Image& image) {
            return cache.ImageAllocationBytes(image);
        };
        const auto GetScaledImageSizeBytes = [&](const ImageBase& image) {
            return cache.GetScaledImageSizeBytes(image);
        };
#include "texture_gc_budget.inc"
        return {critical_memory, expected_memory, other_memory};
    }
    static u64 CostlyDeletes(const Cache& cache) { return cache.gc_costly_deletes; }
    static u64 Deletes(const Cache& cache) { return cache.gc_deletes; }
    static void PendingCpuWrite(Cache& cache, DAddr address) {
        cache.pending_cpu_writes = [address](const auto& visit) { visit(address, 1); };
    }
    template <typename Visitor>
    static void SparseSegments(Cache& cache, ImageBase& image, Visitor&& visitor) {
        cache.ForEachSparseSegment(image, std::forward<Visitor>(visitor));
    }
    static void Unregister(Cache& cache, ImageId id) {
        cache.UnregisterImage(id);
    }
    static void Check(const Cache& cache, size_t address_space, ImageId id, DAddr cpu,
                      bool sparse, bool present) {
        const auto storage = cache.getStorageID(address_space).value();
        const auto& gpu = cache.gpu_page_table_storage[storage * 2].at(1);
        CHECK((std::ranges::find(gpu, id) != gpu.end()) == present);
        if (sparse) {
            const auto& pages = cache.gpu_page_table_storage[storage * 2 + 1].at(1);
            CHECK((std::ranges::find(pages, id) != pages.end()) == present);
        }
        CHECK(cache.page_table.at(cpu >> 20).empty() == !present);
    }
};
}

TEST_CASE("Texture spill collection backs off without pretending an allocation failed",
          "[video_core][texture_spill_collection]") {
    enum class Pressure { Spill, Oom, Critical };
    const auto pressure = GENERATE(Pressure::Spill, Pressure::Oom, Pressure::Critical);
    const bool cheap_progress = GENERATE(false, true);
    const bool pending_readback = GENERATE(false, true);
    ScopedSystemTestContext context;
    Core::System system;
    system.Initialize();
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device{physical};
    Tegra::MemoryManager memory{system, device, 32, 1ULL << 31, 16};
    Tegra::Control::ChannelState channel{0};
    channel.memory_manager = &memory;
    channel.payload.emplace(system, memory, channel);
    Runtime runtime;
    Cache cache{runtime, device};
    cache.CreateChannel(channel);
    cache.BindToChannel(channel.bind_id);
    using Access = VideoCommon::TextureCacheTestAccess;
    Access::ConfigureGc(cache, pressure == Pressure::Critical ? 100000 : 60000);
    // A decode in flight must survive even genuine OOM collection.
    Access::AddGcImage(cache, memory.GetID(), 12, 100,
                       VideoCommon::ImageFlagBits::CostlyLoad | VideoCommon::ImageFlagBits::IsDecoding);
    if (cheap_progress) {
        Access::AddGcImage(cache, memory.GetID(), 13, 100,
                           pending_readback ? VideoCommon::ImageFlagBits::GpuModified
                                            : VideoCommon::ImageFlagBits{});
        if (pending_readback) {
            // A CPU write racing the queued readback wins: the real landing policy rejects it.
            Access::PendingCpuWrite(cache, 0x200000 + (13 << 20));
        }
    }
    for (size_t i = 0; i < 12; ++i) {
        Access::AddGcImage(cache, memory.GetID(), i, 180, VideoCommon::ImageFlagBits::CostlyLoad);
    }
    runtime.failures = pressure == Pressure::Oom ? 1 : 0;
    Access::FrameGc(cache, pressure == Pressure::Spill);
    if (pressure == Pressure::Spill) {
        CHECK(Access::CostlyDeletes(cache) == (cheap_progress ? 0 : 1));
    } else if (pressure == Pressure::Oom) {
        CHECK(Access::CostlyDeletes(cache) == 12);
    } else {
        CHECK(Access::CostlyDeletes(cache) > 0);
        CHECK(Access::CostlyDeletes(cache) < 12);
    }
    CHECK(Access::Deletes(cache) == (cheap_progress && !pending_readback ? 1 : 0));
    CHECK(runtime.downloads == (cheap_progress && pending_readback ? 1 : 0));
    CHECK(runtime.waits == runtime.downloads);
    CHECK(runtime.freed == runtime.downloads);
}

TEST_CASE("Texture removal uses its owner's GPU tables after another address space is bound",
          "[texture_address_space]") {
    const bool sparse = GENERATE(false, true);
    const bool close_owner = GENERATE(false, true);
    ScopedSystemTestContext context;
    Core::System system;
    system.Initialize();
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device{physical};
    Tegra::MemoryManager first{system, device, 32, 1ULL << 31, 16};
    Tegra::MemoryManager second{system, device, 32, 1ULL << 31, 16};
    Tegra::Control::ChannelState a{0};
    Tegra::Control::ChannelState b{1};
    a.memory_manager = &first;
    b.memory_manager = &second;
    a.payload.emplace(system, first, a);
    b.payload.emplace(system, second, b);
    Runtime runtime;
    Cache cache{runtime, device};
    cache.CreateChannel(a);
    cache.CreateChannel(b);
    cache.BindToChannel(a.bind_id);
    using Access = VideoCommon::TextureCacheTestAccess;
    const auto owner = Access::Register(cache, first.GetID(), 0x200000, sparse);
    cache.BindToChannel(b.bind_id);
    const auto other = Access::Register(cache, second.GetID(), 0x300000, sparse);
    if (close_owner) {
        cache.EraseChannel(a.bind_id);
    }
    Access::Unregister(cache, owner);
    Access::Check(cache, first.GetID(), owner, 0x200000, sparse, false);
    Access::Check(cache, second.GetID(), other, 0x300000, sparse, true);
    Access::Unregister(cache, other);
    Access::Check(cache, second.GetID(), other, 0x300000, sparse, false);
}

TEST_CASE("Only a capped share adjusts the texture budget for other allocations",
          "[video_core][texture_budget_share]") {
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device{physical};
    Runtime runtime;
    Cache cache{runtime, device};
    using Access = VideoCommon::TextureCacheTestAccess;
    Access::ConfigureGc(cache);
    using Thresholds = std::array<u64, 3>;
    const Thresholds unadjusted{81920, 40960, 0};
    for (const u64 observed : {100000ULL, 180000ULL, 90000ULL, 160000ULL}) {
        runtime.heap_budget = observed;
        CHECK(Access::BudgetAdjustment(cache, 100000, 20000) == unadjusted);
    }
    CHECK(runtime.heap_reads == 0);

    runtime.capped_share = true;
    runtime.heap_budget = 90000;
    CHECK(Access::BudgetAdjustment(cache, 100000, 20000) == unadjusted);
    runtime.heap_budget = 120000;
    const bool overridden = std::getenv("EDEN_NX2_TC_EXPECTED_MIB") != nullptr ||
                            std::getenv("EDEN_NX2_TC_CRITICAL_MIB") != nullptr;
    CHECK(Access::BudgetAdjustment(cache, 100000, 20000) ==
          (overridden ? Thresholds{81920, 40960, 60000} : Thresholds{100000, 59040, 60000}));
    CHECK(runtime.heap_reads == 2);
}

TEST_CASE("Sparse texture visitors skip invalid translations and retain stop semantics",
          "[video_core][sparse_segment_translation]") {
    ScopedSystemTestContext context;
    Core::System system;
    system.Initialize();
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device{physical};
    Tegra::MemoryManager memory{system, device, 32, 1ULL << 31, 16};
    VideoCore::Test::NullRasterizer rasterizer;
    memory.BindRasterizer(&rasterizer);
    constexpr GPUVAddr gpu = 0x200000;
    const DAddr outside = DAddr{1} << Tegra::MaxwellDeviceMemoryManager::AS_BITS;
    memory.Map(gpu, outside, 0x1000, Tegra::PTEKind::INVALID, false);
    memory.Map(gpu + 0x1000, 0x30000, 0x1000, Tegra::PTEKind::INVALID, false);
    memory.Map(gpu + 0x3000, 0x50000, 0x1000, Tegra::PTEKind::INVALID, false);
    const auto segments = memory.GetSubmappedRange(gpu, 0x4000);
    REQUIRE(segments.size() == 3);
    REQUIRE_FALSE(memory.GpuToCpuAddress(gpu));
    REQUIRE(memory.GpuToCpuAddress(gpu + 0x1000) == 0x30000);
    Tegra::Control::ChannelState channel{0};
    channel.memory_manager = &memory;
    channel.payload.emplace(system, memory, channel);
    Runtime runtime;
    Cache cache{runtime, device};
    cache.CreateChannel(channel);
    cache.BindToChannel(channel.bind_id);
    VideoCommon::ImageBase image{VideoCommon::NullImageParams{}};
    image.gpu_addr = gpu;
    image.guest_size_bytes = 0x4000;
    using Access = VideoCommon::TextureCacheTestAccess;
    using Span = std::array<u64, 3>;
    std::vector<Span> visited;
    const std::vector<Span> expected{{gpu + 0x1000, 0x30000, 0x1000},
                                     {gpu + 0x3000, 0x50000, 0x1000}};
    const auto record = [&](GPUVAddr address, DAddr cpu, size_t size) {
        visited.push_back({address, cpu, size});
    };
    Access::SparseSegments(cache, image, record);
    CHECK(visited == expected);
    visited.clear();
    Access::SparseSegments(cache, image, [&](GPUVAddr address, DAddr cpu, size_t size) {
        record(address, cpu, size);
        return false;
    });
    CHECK(visited == expected);
    visited.clear();
    Access::SparseSegments(cache, image, [&](GPUVAddr address, DAddr cpu, size_t size) {
        record(address, cpu, size);
        return true;
    });
    CHECK(visited == std::vector<Span>{expected.front()});
    memory.Unmap(gpu, 0x4000);
    visited.clear();
    Access::SparseSegments(cache, image, record);
    CHECK(visited.empty());
}
