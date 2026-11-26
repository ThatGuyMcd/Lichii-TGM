// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "tests/core/system_test_context.h"
#include "tests/video_core/null_rasterizer.h"
#include "video_core/texture_cache/texture_cache.h"
#include "video_core/textures/decoders.h"
#include "padded_depth_presence.inc"

namespace {
struct Runtime {
    struct Download {
        std::shared_ptr<std::vector<u8>> bytes;
        std::span<u8> mapped_span;
        size_t offset{};
    };
    u32 failures = 0;
    size_t downloads = 0;
    size_t waits = 0;
    size_t freed = 0;
    size_t feedback_barriers = 0;
    bool capped_share = false;
    bool HasBrokenTextureViewFormats() const { return false; }
    bool HasNativeBgr() const { return false; }
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
    void BarrierFeedbackLoop() { ++feedback_barriers; }
};
struct Image : VideoCommon::ImageBase {
    using ImageBase::ImageBase;
    std::vector<u32> payload;
    std::shared_ptr<int> lifetime;
    template <typename Copies>
    void DownloadMemory(const Runtime::Download& download, const Copies&) {
        if (!payload.empty()) {
            const auto dst = download.mapped_span.subspan(download.offset);
            REQUIRE(dst.size() >= payload.size() * sizeof(u32));
            std::memcpy(dst.data(), payload.data(), payload.size() * sizeof(u32));
        }
    }
};
struct ImageView : VideoCommon::ImageViewBase {
    ImageView(Runtime&, VideoCommon::NullImageViewParams params) : ImageViewBase(params) {}
    template <typename Images>
    ImageView(Runtime&, const VideoCommon::ImageViewInfo& info, VideoCommon::ImageId id,
              const Image& image, Images&)
        : ImageViewBase(info, image.info, id, image.gpu_addr) {}
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

struct ClockedRuntime : Runtime {
    u64 current_tick{1};
    u64 completed_tick{};
    u64 freed_staging_tick{};
    size_t uploaded_slices{};
    u64 CurrentTick() const { return current_tick; }
    bool IsFree(u64 tick) const { return tick <= completed_tick; }
    Download UploadStagingBuffer(size_t size, bool deferred) {
        REQUIRE(deferred);
        auto bytes = std::make_shared<std::vector<u8>>(size);
        return {bytes, *bytes};
    }
    void FreeDeferredStagingBuffer(const Download&) { freed_staging_tick = current_tick; }
    template <typename Copies>
    void AccelerateImageUpload(Image&, const Download&, const Copies&, u32, u32 slices) {
        uploaded_slices += slices;
    }
};

struct LifetimeParams : Params {
    static constexpr bool IMPLEMENTS_ASYNC_DOWNLOADS = true;
    using Runtime = ClockedRuntime;
    using AsyncBuffer = Runtime::Download;
};
using LifetimeCache = VideoCommon::TextureCache<LifetimeParams>;

struct LifetimeFixture {
    static constexpr GPUVAddr GpuAddress = 0x400000;
    static constexpr DAddr CpuAddress = 0x30000;
    static constexpr size_t MappingSize = 0x10000;

    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device{physical};
    VideoCore::Test::NullRasterizer rasterizer;
    ClockedRuntime runtime;
    std::unique_ptr<Kernel::KProcess> process;
    Core::Asid asid{};
    std::unique_ptr<Tegra::MemoryManager> memory;
    Tegra::Control::ChannelState channel{0};
    std::unique_ptr<LifetimeCache> cache;
    u8* bytes{};

    LifetimeFixture() {
        REQUIRE(system.Initialize());
        process = std::make_unique<Kernel::KProcess>(system.Kernel());
        auto& table = process->GetPageTable().GetImpl();
        table.Resize(24, 12);
        bytes = physical.GetPointerFromRaw<u8>(CpuAddress);
        std::memset(bytes, 0, MappingSize);
        for (size_t page = 0; page < MappingSize / 0x1000; ++page) {
            table.entries.GetAndFault((CpuAddress >> 12) + page)
                .Store(false, Common::PageType::Memory, 1,
                       reinterpret_cast<uintptr_t>(bytes) - CpuAddress);
        }
        process->GetMemory().SetCurrentPageTable(*process);
        asid = device.RegisterProcess(&process->GetMemory());
        device.BindInterface(&rasterizer);
        device.Map(CpuAddress, CpuAddress, MappingSize, asid);
        memory = std::make_unique<Tegra::MemoryManager>(system, device, 32, 1ULL << 31, 16);
        memory->BindRasterizer(&rasterizer);
        memory->Map(GpuAddress, CpuAddress, MappingSize, Tegra::PTEKind::INVALID, false);
        channel.memory_manager = memory.get();
        channel.payload.emplace(system, *memory, channel);
        cache = std::make_unique<LifetimeCache>(runtime, device);
        cache->CreateChannel(channel);
        cache->BindToChannel(channel.bind_id);
    }

    ~LifetimeFixture() {
        cache.reset();
        memory.reset();
        device.Unmap(CpuAddress, MappingSize);
        device.UnregisterProcess(asid);
        system.Kernel().UnregisterKernelObject(process.get());
    }
};

VideoCommon::ImageInfo LifetimeImageInfo() {
    VideoCommon::ImageInfo info{};
    info.format = VideoCore::Surface::PixelFormat::A8B8G8R8_UNORM;
    info.type = VideoCommon::ImageType::e2D;
    info.size = {32, 8, 1};
    info.resources = {1, 1};
    return info;
}
}

namespace VideoCommon {
struct TextureCacheTestAccess {
    static ImageId RegisterLifetimeImage(LifetimeCache& cache, const ImageInfo& info, GPUVAddr gpu,
                                         DAddr cpu) {
        const auto id = cache.slot_images.insert(info, gpu, cpu);
        const auto alloc = cache.slot_image_allocs.insert();
        cache.image_allocs_table.emplace(gpu, alloc);
        cache.slot_image_allocs[alloc].images.push_back(id);
        cache.RegisterImage(id);
        return id;
    }
    static Image& LifetimeImage(LifetimeCache& cache, ImageId id) { return cache.slot_images[id]; }
    static void RetireLifetimeImage(LifetimeCache& cache, ImageId id, bool immediate = false) {
        cache.UnregisterImage(id);
        cache.DeleteImage(id, immediate);
    }
    static void QueueLifetimeDownload(LifetimeCache& cache, ImageId id) {
        cache.uncommitted_downloads.emplace_back(LifetimeCache::PendingDownload{true, 0, id});
    }
    static void ReapLifetimeImages(LifetimeCache& cache) { cache.ReapRetiredImages(); }
    static u64 RetiredLifetimeBytes(const LifetimeCache& cache) {
        return cache.gpu_sentenced_bytes;
    }
    static void QueueLifetimeUnswizzle(LifetimeCache& cache, ImageId id) {
        cache.QueueAsyncUnswizzle(cache.slot_images[id], id);
    }
    static void ProcessOneUnswizzleSlice(LifetimeCache& cache) {
        cache.current_unswizzle_frame = 0;
        cache.swizzle_chunk_size = 512;
        cache.swizzle_slices_per_batch = 1;
        cache.TickAsyncUnswizzle();
    }
    static std::vector<ImageId> QueuedLifetimeUnswizzles(const LifetimeCache& cache) {
        std::vector<ImageId> ids;
        for (const auto& task : cache.unswizzle_queue) {
            ids.push_back(task.image_id);
        }
        return ids;
    }

    using Maxwell3D = Tegra::Engines::Maxwell3D;
    static ImageInfo ConfigureDepth(Cache& cache, Core::System& system, u32 width, u32 height) {
        using Maxwell = Tegra::Engines::Maxwell3D;
        VideoCommon::Dirty::SetupDirtyFlags(cache.maxwell3d->dirty.tables);
        auto& regs = cache.maxwell3d->regs;
        regs.zeta_enable = 1;
        regs.zeta.address_low = 0x200000;
        regs.zeta.address_high = 0;
        regs.zeta.format = Tegra::DepthFormat::Z32_FLOAT;
        regs.reg_array[MAXWELL3D_REG_INDEX(zeta.tile_mode)] = 0;
        regs.zeta.tile_mode.block_height.Assign(4);
        regs.zeta_size.width = width;
        regs.zeta_size.height = height;
        regs.zeta_size.depth.Assign(1);
        regs.zeta_size.dim_control.Assign(Maxwell::Regs::ZetaSize::DimensionControl::ArraySizeIsOne);
        regs.anti_alias_samples_mode = Tegra::Texture::MsaaMode::Msaa1x1;
        Clip(cache, system, height, height);
        return ImageInfo(regs.zeta, regs.zeta_size, regs.anti_alias_samples_mode);
    }
    static void Clip(Cache& cache, Core::System& system, u32 width, u32 height) {
        cache.maxwell3d->CallMethod(system, MAXWELL3D_REG_INDEX(surface_clip), width << 16, true);
        cache.maxwell3d->CallMethod(system, MAXWELL3D_REG_INDEX(surface_clip) + 1, height << 16, true);
    }
    static auto& Flags(Cache& cache) { return cache.maxwell3d->dirty.flags; }
    static ImageId RegisterDepth(Cache& cache, const ImageInfo& info, GPUVAddr gpu = 0x200000,
                                  DAddr cpu = 0x400000) {
        const auto id = cache.slot_images.insert(info, gpu, cpu);
        auto& image = cache.slot_images[id];
        image.flags |= ImageFlagBits::Strong;
        image.payload.resize(static_cast<size_t>(info.size.width) * info.size.height, 0xdecafbad);
        cache.RegisterImage(id);
        return id;
    }
    static Image& Get(Cache& cache, ImageId id) { return cache.slot_images[id]; }
    static ImageId Find(Cache& cache, const ImageInfo& info) {
        return cache.FindImage(info, 0x200000, RelaxedOptions{});
    }
    static ImageViewId DepthLookup(Cache& cache, std::optional<ImageInfo>& requested,
                                   bool allocate = true) {
        auto* maxwell3d = cache.maxwell3d;
#if HAS_PADDED_DEPTH_GATE
        auto& depth_target_padded = cache.depth_target_padded;
        const auto NarrowPaddedDepthTarget = [&](const ImageInfo& info, GPUVAddr gpu) {
            return cache.NarrowPaddedDepthTarget(info, gpu);
        };
#endif
        const auto FindRenderTargetView = [&](const ImageInfo& info, GPUVAddr gpu) {
            requested = info;
            if (!allocate) return ImageViewId{};
            auto image = cache.FindImage(info, gpu, RelaxedOptions{});
            if (!image) image = RegisterDepth(cache, info, gpu);
            const ImageViewInfo view{ImageViewType::e2D, info.format,
                                     SubresourceRange{{0, 0}, {1, 1}}};
            return cache.FindOrEmplaceImageView(image, view);
        };
#include "padded_depth_lookup.inc"
    }
    static std::optional<ImageInfo> RefreshDepth(Cache& cache, bool force = false) {
        using namespace VideoCommon::Dirty;
#if HAS_PADDED_DEPTH_GATE
        const auto InvalidatePaddedDepthTargetForResolution = [&] {
            cache.InvalidatePaddedDepthTargetForResolution();
        };
#endif
#include "padded_depth_invalidation.inc"
        auto& flags = cache.maxwell3d->dirty.flags;
        if (!flags[Dirty::RenderTargets] && !force) return std::nullopt;
        auto& render_targets = cache.render_targets;
#if HAS_PADDED_DEPTH_GATE
        const auto depth_target_padded = cache.depth_target_padded;
#endif
        std::optional<ImageInfo> requested;
        const auto FindDepthBuffer = [&] { return DepthLookup(cache, requested); };
        const auto BindRenderTarget = [&](ImageViewId* old, ImageViewId next) {
            cache.BindRenderTarget(old, next);
        };
#include "padded_depth_refresh.inc"
        return requested;
    }
    static ImageId BoundDepth(Cache& cache) {
        return cache.slot_image_views[cache.render_targets.depth_buffer_id].image_id;
    }
    static auto& DepthRegs(Cache& cache) { return cache.maxwell3d->regs; }
    static Extent3D BoundDepthSize(Cache& cache) {
        return cache.slot_image_views[cache.render_targets.depth_buffer_id].size;
    }
    static ImageViewId View(Cache& cache, ImageId image) {
        const auto id = cache.slot_image_views.insert(cache.runtime, NullImageViewParams{});
        cache.slot_image_views[id].image_id = image;
        return id;
    }
    static void RetireView(Cache& cache, ImageViewId id) {
        cache.RemoveImageViewReferences(std::span{&id, 1});
        cache.slot_image_views.erase(id);
    }
    static void FeedbackTargets(Cache& cache, ImageViewId depth, ImageViewId color = {}) {
        cache.render_targets.depth_buffer_id = depth;
        cache.render_targets.color_buffer_ids[0] = color;
        cache.rt_depth_image_id = depth ? cache.slot_image_views[depth].image_id : ImageId{};
        cache.rt_image_id[0] = color ? cache.slot_image_views[color].image_id : ImageId{};
        const u32 depth_bit = 1U << cache.rt_image_id.size();
        cache.rt_active_mask = (depth ? depth_bit : 0U) | (color ? 1U : 0U);
        ++cache.render_targets_serial;
    }
    static void WritesDepth(Cache& cache, bool value) {
        cache.maxwell3d->regs.depth_write_enabled = value;
    }
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

TEST_CASE("A retired image counts against memory until both timelines release it",
          "[video_core][texture_image_lifetime]") {
    using Access = VideoCommon::TextureCacheTestAccess;
    LifetimeFixture f;
    auto& cache = *f.cache;
    const auto info = LifetimeImageInfo();
    const auto id = Access::RegisterLifetimeImage(cache, info, f.GpuAddress, f.CpuAddress);
    auto& image = Access::LifetimeImage(cache, id);
    image.lifetime = std::make_shared<int>();
    const std::weak_ptr<int> lifetime = image.lifetime;
    image.payload.assign(image.unswizzled_size_bytes / sizeof(u32), 0x51515151);
    Access::QueueLifetimeDownload(cache, id);

    const bool download_first = GENERATE(false, true);
    if (download_first) {
        cache.CommitAsyncFlushes();
    }
    f.runtime.current_tick = 7;
    Access::RetireLifetimeImage(cache, id, true);
    REQUIRE(Access::RegisterLifetimeImage(cache, info, f.GpuAddress + 0x4000,
                                          f.CpuAddress + 0x4000) == id);
    const u64 retained_bytes = Access::RetiredLifetimeBytes(cache);
    CHECK(retained_bytes > 0);
    if (download_first) {
        f.runtime.completed_tick = 1;
        cache.PopAsyncFlushes();
        Access::ReapLifetimeImages(cache);
        CHECK_FALSE(lifetime.expired());
        CHECK(Access::RetiredLifetimeBytes(cache) == retained_bytes);
        CHECK(f.bytes[0] == 0x51);
        f.runtime.completed_tick = 7;
    } else {
        f.runtime.completed_tick = 7;
        Access::ReapLifetimeImages(cache);
        CHECK_FALSE(lifetime.expired());
        CHECK(Access::RetiredLifetimeBytes(cache) == retained_bytes);
        CHECK(f.bytes[0] == 0);
        f.runtime.current_tick = 8;
        cache.CommitAsyncFlushes();
        f.runtime.completed_tick = 8;
        cache.PopAsyncFlushes();
    }
    Access::ReapLifetimeImages(cache);
    CHECK(lifetime.expired());
    CHECK(Access::RetiredLifetimeBytes(cache) == 0);
    CHECK(f.bytes[0] == 0x51);
    CHECK(f.bytes[0x4000] == 0);
}

TEST_CASE("Retiring an image drops only its queued GPU unswizzle",
          "[video_core][texture_image_lifetime]") {
    using Access = VideoCommon::TextureCacheTestAccess;
    using VideoCommon::ImageType;
    using VideoCore::Surface::PixelFormat;
    LifetimeFixture f;
    auto& cache = *f.cache;
    auto volume = LifetimeImageInfo();
    volume.type = ImageType::e3D;
    volume.format = PixelFormat::BC1_RGBA_UNORM;
    volume.size = {32, 32, 8};
    const auto retiring = Access::RegisterLifetimeImage(cache, volume, f.GpuAddress, f.CpuAddress);
    const auto queued = Access::RegisterLifetimeImage(cache, volume, f.GpuAddress + 0x4000,
                                                       f.CpuAddress + 0x4000);
    auto& image = Access::LifetimeImage(cache, retiring);
    image.lifetime = std::make_shared<int>();
    const std::weak_ptr<int> lifetime = image.lifetime;
    Access::QueueLifetimeUnswizzle(cache, retiring);
    Access::QueueLifetimeUnswizzle(cache, queued);
    const bool submitted = GENERATE(false, true);
    if (submitted) {
        f.runtime.current_tick = 4;
        Access::ProcessOneUnswizzleSlice(cache);
        REQUIRE(f.runtime.uploaded_slices == 1);
    }

    f.runtime.current_tick = 5;
    Access::RetireLifetimeImage(cache, retiring, true);
    REQUIRE(Access::RegisterLifetimeImage(cache, LifetimeImageInfo(), f.GpuAddress + 0x8000,
                                          f.CpuAddress + 0x8000) == retiring);
    CHECK(Access::QueuedLifetimeUnswizzles(cache) == std::vector{queued});
    CHECK(f.runtime.freed_staging_tick == (submitted ? 5u : 0u));
    CHECK(lifetime.expired() == !submitted);
    CHECK((Access::RetiredLifetimeBytes(cache) > 0) == submitted);
    f.runtime.completed_tick = 5;
    Access::ReapLifetimeImages(cache);
    CHECK(lifetime.expired());
    CHECK(Access::RetiredLifetimeBytes(cache) == 0);
}

TEST_CASE("Depth feedback follows the views bound by each draw",
          "[video_core][bound_view_feedback]") {
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
    using VideoCommon::ImageViewInOut;
    const auto depth_view = Access::View(cache, VideoCommon::ImageId{120});
    const auto depth_sample = Access::View(cache, VideoCommon::ImageId{120});
    const auto other_sample = Access::View(cache, VideoCommon::ImageId{121});
    const std::array depth{ImageViewInOut{.id = depth_sample}};
    const std::array other{ImageViewInOut{.id = other_sample}};
    const std::array attachment{ImageViewInOut{.id = depth_view}};
    REQUIRE(Settings::values.barrier_feedback_loops.GetValue());
    Access::WritesDepth(cache, false);
    Access::FeedbackTargets(cache, depth_view);

    const u64 bound_view_serial = cache.ImageViewRemovalSerial();
    cache.CheckFeedbackLoop(other, bound_view_serial);
    cache.CheckFeedbackLoop(attachment, bound_view_serial);
    CHECK(runtime.feedback_barriers == 0);
    CHECK_FALSE(cache.SamplesRenderTarget(other, bound_view_serial));
    CHECK(cache.SamplesRenderTarget(depth, bound_view_serial));
    cache.CheckFeedbackLoop(depth, bound_view_serial);
    CHECK(runtime.feedback_barriers == 1);
    cache.CheckFeedbackLoop(depth, bound_view_serial);
    CHECK(runtime.feedback_barriers == 1);

    Access::WritesDepth(cache, true);
    cache.CheckFeedbackLoop(other, bound_view_serial);
    Access::WritesDepth(cache, false);
    cache.CheckFeedbackLoop(depth, bound_view_serial);
    CHECK(runtime.feedback_barriers == 2);

    Access::FeedbackTargets(cache, depth_view, depth_sample);
    cache.CheckFeedbackLoop(depth, bound_view_serial);
    CHECK(runtime.feedback_barriers == 2);

    // The draw bound the view before the render-target work retired its slot.
    Access::FeedbackTargets(cache, depth_view);
    Access::RetireView(cache, depth_sample);
    CHECK(cache.ImageViewRemovalSerial() != bound_view_serial);
    cache.CheckFeedbackLoop(depth, bound_view_serial);
    CHECK(runtime.feedback_barriers == 3);
    CHECK(cache.SamplesRenderTarget(depth, bound_view_serial));

    REQUIRE(Access::View(cache, VideoCommon::ImageId{122}) == depth_sample);
    const u64 fresh_view_serial = cache.ImageViewRemovalSerial();
    cache.CheckFeedbackLoop(depth, fresh_view_serial);
    CHECK(runtime.feedback_barriers == 3);
    CHECK_FALSE(cache.SamplesRenderTarget(depth, fresh_view_serial));
    CHECK(cache.SamplesRenderTarget(depth, bound_view_serial));

    Access::FeedbackTargets(cache, {});
    cache.CheckFeedbackLoop(depth, bound_view_serial);
    CHECK(runtime.feedback_barriers == 3);
    CHECK_FALSE(cache.SamplesRenderTarget(depth, bound_view_serial));
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

namespace {
struct PaddedDepthFixture {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device{physical};
    VideoCore::Test::NullRasterizer rasterizer;
    Runtime runtime;
    std::unique_ptr<Tegra::MemoryManager> memory;
    Tegra::Control::ChannelState channel{0};
    std::unique_ptr<Cache> cache;
    const Settings::ResolutionScalingInfo saved_resolution = Settings::values.resolution_info;
    PaddedDepthFixture() {
        system.Initialize();
        Settings::values.resolution_info.up_scale = 1;
        Settings::values.resolution_info.down_shift = 0;
        memory = std::make_unique<Tegra::MemoryManager>(system, device, 32, 1ULL << 31, 16);
        memory->BindRasterizer(&rasterizer);
        memory->Map(0x200000, 0x400000, 0x400000, Tegra::PTEKind::INVALID, false);
        channel.memory_manager = memory.get();
        channel.payload.emplace(system, *memory, channel);
        cache = std::make_unique<Cache>(runtime, device);
        cache->CreateChannel(channel);
        cache->BindToChannel(channel.bind_id);
    }
    ~PaddedDepthFixture() { Settings::values.resolution_info = saved_resolution; }
};
}

TEST_CASE("Padded depth lookup reuses the square image and follows clip dirty writes",
          "[video_core][padded_depth_cache]") {
    using Access = VideoCommon::TextureCacheTestAccess;
    PaddedDepthFixture f;
    auto& cache = *f.cache;
    const auto wide = Access::ConfigureDepth(cache, f.system, 800, 793);
    const auto first = Access::RefreshDepth(cache, true);
    REQUIRE(first);
    REQUIRE(first->size.width == 793);
    CHECK(VideoCommon::CalculateGuestSizeInBytes(*first) == VideoCommon::CalculateGuestSizeInBytes(wide));
    const auto square_id = Access::BoundDepth(cache);
    CHECK(Access::BoundDepthSize(cache).width == 793);
    CHECK(Access::Find(cache, *first) == square_id);
    CHECK_FALSE(Access::RefreshDepth(cache));
    auto& square = Access::Get(cache, square_id);
    square.flags |= VideoCommon::ImageFlagBits::GpuModified;
    square.payload.assign(square.payload.size(), 0x12345678);
    const auto square_payload = square.payload;

    // A later wider owner can contain live padding. Do not replace or narrow it.
    // Allocation/driver upload is a boundary here; registration and lookups are real.
    const auto wide_id = Access::RegisterDepth(cache, wide);
    auto& wider = Access::Get(cache, wide_id);
    wider.flags |= VideoCommon::ImageFlagBits::GpuModified;
    for (size_t i = 0; i < wider.payload.size(); ++i) wider.payload[i] = u32(i ^ 0x8badf00d);
    const auto wider_payload = wider.payload;
    Access::Flags(cache).reset();
    Access::Clip(cache, f.system, 800, 793);
    CHECK(Access::Flags(cache)[VideoCommon::Dirty::RenderTargets]);
    CHECK_FALSE(Access::Flags(cache)[VideoCommon::Dirty::ZetaBuffer]);
    const auto expanded = Access::RefreshDepth(cache);
    REQUIRE(expanded);
    CHECK(expanded->size.width == 800);
    CHECK(Access::BoundDepth(cache) == wide_id);
    Access::Flags(cache).reset();
    Access::Clip(cache, f.system, 793, 793);
    const auto returned = Access::RefreshDepth(cache);
    REQUIRE(returned);
    CHECK(returned->size.width == 800);
    CHECK(Access::BoundDepth(cache) == wide_id);
    CHECK(Access::Get(cache, wide_id).payload == wider_payload);
    CHECK(Access::Get(cache, square_id).payload == square_payload);
    CHECK(True(Access::Get(cache, wide_id).flags & VideoCommon::ImageFlagBits::Registered));
    CHECK(False(Access::Get(cache, wide_id).flags & VideoCommon::ImageFlagBits::Picked));
}

TEST_CASE("Padded depth resolution changes invalidate an otherwise clean binding",
          "[video_core][padded_depth_cache]") {
    using Access = VideoCommon::TextureCacheTestAccess;
    PaddedDepthFixture f;
    auto& cache = *f.cache;
    Access::ConfigureDepth(cache, f.system, 800, 793);
    REQUIRE(Access::RefreshDepth(cache, true));
    Access::Flags(cache).reset();
    Settings::values.resolution_info.up_scale = 2;
    const auto scaled = Access::RefreshDepth(cache);
    REQUIRE(scaled);
    CHECK(scaled->size.width == 800);
    CHECK_FALSE(Access::RefreshDepth(cache));
    Settings::values.resolution_info.up_scale = 1;
    const auto restored = Access::RefreshDepth(cache);
    REQUIRE(restored);
    CHECK(restored->size.width == 800); // The new wider owner remains authoritative.
    CHECK_FALSE(Access::RefreshDepth(cache));
}

TEST_CASE("Padded depth size overflow and device-range overflow are rejected before allocation",
          "[video_core][padded_depth_cache]") {
    using Access = VideoCommon::TextureCacheTestAccess;
    PaddedDepthFixture f;
    auto& cache = *f.cache;
    const auto huge = Access::ConfigureDepth(cache, f.system, 32768, 32767);
    REQUIRE(VideoCommon::CalculateGuestSizeInBytes(huge) == 0);
    std::optional<VideoCommon::ImageInfo> request;
    Access::DepthLookup(cache, request, false);
    REQUIRE(request);
    CHECK(request->size.width == 32768);
    Access::ConfigureDepth(cache, f.system, 800, 793);
    f.memory->Unmap(0x200000, 0x400000);
    constexpr DAddr limit = DAddr{1} << Tegra::MaxwellDeviceMemoryManager::AS_BITS;
    f.memory->Map(0x200000, limit - 0x10000, 0x400000, Tegra::PTEKind::INVALID, false);
    Access::DepthLookup(cache, request, false);
    REQUIRE(request);
    CHECK(request->size.width == 800);
}

TEST_CASE("Padded depth ownership gate preserves incompatible existing images",
          "[video_core][padded_depth_cache]") {
    using Access = VideoCommon::TextureCacheTestAccess;
    using VideoCommon::ImageFlagBits;
    const auto mode = GENERATE(0, 1, 2, 3, 4, 5);
    PaddedDepthFixture f;
    auto& cache = *f.cache;
    const auto wide = Access::ConfigureDepth(cache, f.system, 800, 793);
    auto existing = wide;
    if (mode != 0) existing.size.width = 793;
    const auto image_id = Access::RegisterDepth(cache, existing, mode == 1 ? 0x800000 : 0x200000);
    auto& image = Access::Get(cache, image_id);
    image.flags |= ImageFlagBits::GpuModified;
    if (mode == 2) image.flags |= ImageFlagBits::Remapped;
    if (mode == 3) image.flags |= ImageFlagBits::Sparse;
    if (mode == 4) image.flags |= ImageFlagBits::Rescaled;
    if (mode == 5) image.address_space += 100;
    const auto flags = image.flags;
    const auto payload = image.payload;
    const auto request = Access::RefreshDepth(cache, true);
    REQUIRE(request);
    CHECK(request->size.width == 800);
    CHECK(Access::Get(cache, image_id).flags == flags);
    CHECK(Access::Get(cache, image_id).payload == payload);
}

TEST_CASE("Padded depth exclusions retain descriptor extent",
          "[video_core][padded_depth_cache]") {
    using Access = VideoCommon::TextureCacheTestAccess;
    const auto mode = GENERATE(0, 1, 2, 3, 4, 5, 6);
    PaddedDepthFixture f;
    auto& cache = *f.cache;
    Access::ConfigureDepth(cache, f.system, 800, 793);
    auto& regs = Access::DepthRegs(cache);
    if (mode == 0) regs.zeta.format = Tegra::DepthFormat::Z24_UNORM_S8_UINT;
    if (mode == 1) regs.anti_alias_samples_mode = Tegra::Texture::MsaaMode::Msaa2x2;
    if (mode == 2) {
        regs.zeta_size.dim_control.Assign(Tegra::Engines::Maxwell3D::Regs::ZetaSize::DimensionControl::DefineArraySize);
        regs.zeta_size.depth.Assign(2);
        regs.zeta.array_pitch = 0x100000;
    }
    if (mode == 3) regs.zeta.tile_mode.is_pitch_linear.Assign(1);
    if (mode == 4) Settings::values.resolution_info.up_scale = 2;
    if (mode == 5) Access::Clip(cache, f.system, 0, 793);
    if (mode == 6) f.memory->Unmap(0x210000, 0x10000);
    const auto request = Access::RefreshDepth(cache, true);
    REQUIRE(request);
    CHECK(request->size.width == 800);
}

TEST_CASE("Square D32 swizzle writes preserve padded guest columns",
          "[video_core][padded_depth_cache]") {
    const bool native = GENERATE(false, true);
    constexpr u32 width = 800, height = 793;
    std::vector<u32> initial(width * height);
    for (u32 y = 0; y < height; ++y)
        for (u32 x = 0; x < width; ++x) initial[y * width + x] = (y << 16) | x;
    std::vector<u8> guest(Tegra::Texture::CalculateSize(true, 4, width, height, 1, 4, 0), 0x5b);
    const auto bytes = [](const auto& v) { return std::span<const u8>(reinterpret_cast<const u8*>(v.data()), v.size() * sizeof(v[0])); };
    Tegra::Texture::SwizzleTexture(guest, bytes(initial), 4, width, height, 1, 4, 0, 1, native);
    std::vector<u32> square(height * height, 0x13579bdf);
    Tegra::Texture::SwizzleTexture(guest, bytes(square), 4, height, height, 1, 4, 0, 1, native);
    std::vector<u32> result(width * height);
    Tegra::Texture::UnswizzleTexture(std::span<u8>(reinterpret_cast<u8*>(result.data()), result.size() * 4),
                                    guest, 4, width, height, 1, 4, 0, 1, native);
    for (u32 y = 0; y < height; ++y) {
        REQUIRE(std::all_of(result.begin() + y * width, result.begin() + y * width + height,
                            [](u32 v) { return v == 0x13579bdf; }));
        REQUIRE(std::equal(result.begin() + y * width + height, result.begin() + (y + 1) * width,
                           initial.begin() + y * width + height));
    }
}
