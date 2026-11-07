// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include "common/container/unordered_map.h"
#include "common/container/unordered_set.h"
#include <vector>
#include "common/container/unordered_map.h"

#include "common/common_types.h"
#include "common/thread_worker.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/value.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/object_pool.h"
#include "shader_recompiler/profile.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/renderer_vulkan/async_draw_policy.h"
#include "video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "video_core/renderer_vulkan/vk_buffer_cache.h"
#include "video_core/renderer_vulkan/vk_compute_pipeline.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_shader_reuse.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache_prebuild.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/shader_cache.h"

namespace Core {
class System;
}

namespace Shader::IR {
struct Program;
}

namespace VideoCore {
class ShaderNotify;
}

namespace VideoCommon::ShaderWarm {
class PreparedStore;
}

namespace Vulkan {

using Maxwell = Tegra::Engines::Maxwell3D::Regs;

struct ComputePipelineCacheKey {
    u64 unique_hash;
    u32 shared_memory_size;
    std::array<u32, 3> workgroup_size;

    size_t Hash() const noexcept;

    bool operator==(const ComputePipelineCacheKey& rhs) const noexcept;

    bool operator!=(const ComputePipelineCacheKey& rhs) const noexcept {
        return !operator==(rhs);
    }
};
static_assert(std::has_unique_object_representations_v<ComputePipelineCacheKey>);
static_assert(std::is_trivially_copyable_v<ComputePipelineCacheKey>);
static_assert(std::is_trivially_constructible_v<ComputePipelineCacheKey>);

} // namespace Vulkan

namespace std {

template <>
struct hash<Vulkan::ComputePipelineCacheKey> {
    size_t operator()(const Vulkan::ComputePipelineCacheKey& k) const noexcept {
        return k.Hash();
    }
};

} // namespace std

namespace Vulkan {

class ComputePipeline;
class DescriptorPool;
class Device;
class PipelineStatistics;
class RenderPassCache;
class Scheduler;

using VideoCommon::ShaderInfo;

struct ShaderPools {
    void ReleaseContents() {
        flow_block.ReleaseContents();
        block.ReleaseContents();
        inst.ReleaseContents();
    }

    Shader::ObjectPool<Shader::IR::Inst> inst{8192};
    Shader::ObjectPool<Shader::IR::Block> block{32};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_block{32};
};

struct ComputePipelineCacheEntry {
    std::unique_ptr<ComputePipeline> pipeline;
    std::string failure;
    /// Constant buffer slots that were null pointers at translation (see NullStoragePointerSlots)
    std::vector<std::pair<u32, u32>> null_pointer_slots;
    /// Constant buffer slots bound as storage buffer pointers (see MappedStoragePointerKeys)
    std::vector<u64> mapped_pointers;
    u32 rebuilds{};
    /// Whether a dispatch looked it up yet (kept only with EDEN_NX2_STUTTER_TRACE)
    bool used{};
    /// A rebuild whose driver compile runs on a worker; this entry's pipeline is used until it is
    /// built (a compile waited on by the dispatch stalled every GPU channel for 2-3 s)
    std::unique_ptr<ComputePipelineCacheEntry> pending_rebuild;

    [[nodiscard]] ComputePipeline* Get(bool native, GPUVAddr address,
                                       const ComputePipelineCacheKey& key) const;
};

class PipelineCache : public VideoCommon::ShaderCache {
public:
    explicit PipelineCache(Tegra::MaxwellDeviceMemoryManager& device_memory_, const Device& device,
                           Scheduler& scheduler, DescriptorPool& descriptor_pool,
                           GuestDescriptorQueue& guest_descriptor_queue,
                           DescriptorBufferRing& descriptor_buffer_ring,
                           RenderPassCache& render_pass_cache, BufferCache& buffer_cache,
                           TextureCache& texture_cache, VideoCore::ShaderNotify& shader_notify_);
    ~PipelineCache();

    [[nodiscard]] GraphicsPipeline* CurrentGraphicsPipeline();

    /// Whether the guest's HMMA runs on the host's cooperative matrices (the self-test passed).
    [[nodiscard]] bool SupportsCooperativeHmma() const noexcept {
        return profile.support_cooperative_hmma || profile.support_cooperative_hmma_16x16x16;
    }

    void TickFrame();

    [[nodiscard]] ComputePipeline* CurrentComputePipeline(u64& native_key);

    void LoadDiskResources(u64 title_id, std::stop_token stop_loading,
                           const VideoCore::DiskResourceLoadCallback& callback);

private:
    [[nodiscard]] GraphicsPipeline* CurrentGraphicsPipelineSlowPath();

    /// Returns `pipeline` when it is built or its draw must wait for it, nullptr when the draw
    /// may be skipped while it builds (asynchronous shaders, CanDiscardPendingDraw).
    [[nodiscard]] GraphicsPipeline* BuiltPipeline(GraphicsPipeline* pipeline) noexcept;

    /// How the current draw treats an unbuilt pipeline (native_async_draws, CanDiscardPendingDraw).
    struct PendingDrawPolicy {
        bool native;
        NativePendingMode mode;
        bool can_discard;
    };
    [[nodiscard]] PendingDrawPolicy CurrentPendingDrawPolicy() const;

    /// Charges the time since `begin` to this frame's native wait budget.
    void ChargeNativeWait(std::chrono::steady_clock::time_point begin);

    std::unique_ptr<GraphicsPipeline> CreateGraphicsPipeline();

    struct GraphicsStages;
    /// Translates a graphics pipeline's programs and works out each stage's runtime info (the
    /// work before SPIR-V emission); false when the pipeline is skipped, its draws dropped.
    bool TranslateGraphicsStages(ShaderPools& pools, const GraphicsPipelineCacheKey& key,
                                 std::span<Shader::Environment* const> envs, GraphicsStages& out,
                                 bool reference_spirv);

    std::unique_ptr<GraphicsPipeline> CreateGraphicsPipeline(
        ShaderPools& pools, const GraphicsPipelineCacheKey& key,
        std::span<Shader::Environment* const> envs, PipelineStatistics* statistics,
        bool build_in_parallel, std::shared_ptr<ShaderPools> emit_pools = nullptr,
        std::unique_ptr<EmissionBudget::Reservation> reservation = nullptr,
        std::array<std::vector<u32>, 5>* spirv_out = nullptr,
        bool heavy_on_own_threads = false, bool allow_translation = true, bool speculative = false,
        DescriptorPool* transient_descriptors = nullptr);

    using GraphicsEnvironmentArray =
        std::array<VideoCommon::GraphicsEnvironment, Maxwell::MaxShaderProgram>;

    /// A graphics pipeline translated on a translation worker, waiting for the GPU thread.
    struct TranslatedGraphicsPipeline {
        GraphicsPipelineCacheKey key;
        std::unique_ptr<GraphicsPipeline> pipeline; ///< Null when it is skipped or failed.
        GraphicsEnvironmentArray envs;              ///< For the disk cache.
        /// The detached environments could not answer translation (DetachedEnvironmentMiss).
        bool translate_on_gpu_thread{};
    };

    void SerializeGraphicsPipeline(const GraphicsPipelineCacheKey& key,
                                   GraphicsEnvironmentArray envs);

    /// Whether graphics_key's new pipeline is translated on a translation worker.
    [[nodiscard]] bool CanTranslateAsynchronously() const;

    /// Builds graphics_key's pipeline from shaders an earlier pipeline translated, null if none fit.
    std::unique_ptr<GraphicsPipeline> CreateGraphicsPipelineFromReusedShaders(
        GraphicsEnvironments& environments);

    /// Detaches the environments of graphics_key's programs from the engine and guest memory.
    /// False when one cannot be (the pipeline is then translated on the GPU thread).
    [[nodiscard]] bool DetachGraphicsEnvironments(GraphicsEnvironmentArray& envs);

    /// Queues the translation of graphics_key's pipeline on a translation worker and waits for
    /// it as WaitForGraphicsTranslation does.
    bool QueueGraphicsTranslation(GraphicsEnvironmentArray envs);

    /// Waits for a queued translation as the current draw's pending policy allows: until it is
    /// done for a draw that cannot be skipped, within the native wait budget for a bounded one.
    /// True when the translation finished and its result was adopted.
    bool WaitForGraphicsTranslation(std::shared_future<void> translated);

    /// Runs on a translation worker thread.
    void TranslateGraphicsPipeline(const GraphicsPipelineCacheKey& key,
                                   GraphicsEnvironmentArray envs, std::promise<void> done);

    /// Moves the pipelines finished by the translation workers into graphics_cache (GPU thread).
    void AdoptTranslatedGraphicsPipelines();


    [[nodiscard]] bool StaleNullPointerSlot(const ComputePipelineCacheEntry& entry) const;

    /// Translates the current compute program, binding the pointers in keep_pointers whatever
    /// their slots hold now (see GenericEnvironment::KeepStoragePointers)
    ComputePipelineCacheEntry CreateComputePipeline(const ComputePipelineCacheKey& key,
                                                    const ShaderInfo* shader,
                                                    std::span<const u64> keep_pointers = {});

    ComputePipelineCacheEntry CreateComputePipeline(ShaderPools& pools,
                                                    const ComputePipelineCacheKey& key,
                                                    Shader::Environment& env,
                                                    PipelineStatistics* statistics,
                                                    bool build_in_parallel,
                                                    std::vector<u32>* spirv_out = nullptr);

    struct PrecompiledRecord;

    /// The compute counterpart of CreatePrecompiledGraphicsPipeline: an entry without a
    /// pipeline when the key is not precompiled or its answers differ. A key may have several
    /// records (storage pointer states): the first whose answers hold is used (set to `matched`).
    ComputePipelineCacheEntry CreatePrecompiledComputePipeline(
        const ComputePipelineCacheKey& key, VideoCommon::ComputeEnvironment& env,
        const PrecompiledRecord** matched = nullptr);

    void SerializeVulkanPipelineCache(const std::filesystem::path& filename,
                                      const vk::PipelineCache& pipeline_cache, u32 cache_version);

    vk::PipelineCache LoadVulkanPipelineCache(const std::filesystem::path& filename,
                                              u32 expected_cache_version);

    void QueueVulkanPipelineCacheFlush();

    /// Queues one write of the driver pipeline cache file on the serialization thread, with no
    /// throttle. For the end of a precache: its builds never reach the timed flush, which counts
    /// only pipelines built during play.
    void QueueVulkanPipelineCacheSave();

    /// One build of the shader precache: builds a pipeline and destroys it, true when it built.
    struct PrecacheJob {
        u64 graphics_hash{}; ///< The cached graphics pipeline it builds, 0 for a compute program
        Common::UniqueFunction<bool> build;
        u64 compute_hash{}; ///< The compute program it builds (its unique hash), else 0
    };

    /// Shader precache before the game starts (precache_shaders), on every CPU thread: the cached
    /// graphics pipelines this boot skipped (`jobs`; `precached` holds those built by earlier
    /// precaches) and the compute programs of the title's precache file (made from the game files,
    /// missing reads guessed) are built, then thrown away. They only fill the driver's shader
    /// cache, so a later build of the same pipeline finds the driver's work done. Reports progress
    /// through `callback` and returns when it is done, skipped from the loading screen or the load
    /// is stopped.
    void PrecacheShaders(u64 title_id, std::vector<PrecacheJob> jobs,
                         std::unordered_set<u64> precached, std::stop_token stop_loading,
                         const VideoCore::DiskResourceLoadCallback& callback);

    /// The game's native preparation file (native-preparation.bin, assembled locally from a
    /// recipe and the player's game files) and the journal of what was prepared from it
    /// (native-prepared.journal). With `prepare`, every pipeline not prepared before for this
    /// stamp is built once and dropped, so the driver's cache holds it, and its shader infos and
    /// SPIR-V are kept; then the kept ones are loaded for draws to use.
    void PreparePrecompiled(const std::filesystem::path& base_dir, bool prepare,
                            std::stop_token stop_loading,
                            const VideoCore::DiskResourceLoadCallback& callback);

    /// What the kept translations depend on: the preparation file's contents, the translator's
    /// sources, the cache version, the GPU, driver and pipeline cache UUID, the host profile and
    /// the settings and switches translation reads. Empty when the file can't be read.
    std::string NativePreparationStamp(const std::filesystem::path& path) const;

    void RunNativePreparation(const std::filesystem::path& path,
                              VideoCommon::ShaderWarm::PreparedStore& store,
                              std::stop_token stop_loading,
                              const VideoCore::DiskResourceLoadCallback& callback);

    /// Builds one record's pipeline on a worker and journals its infos and SPIR-V. Throws when
    /// it can't be built or kept.
    void PrepareNativeRecord(const std::filesystem::path& path,
                             std::span<const std::pair<u64, u64>> env_index,
                             VideoCommon::ShaderWarm::PreparedStore& store, u32 index, bool compute,
                             const PrecompiledRecord& record,
                             const GraphicsPipelineCacheKey& graphics,
                             const ComputePipelineCacheKey& compute_key);

    /// The records of `store` whose blobs are journaled, and the preparation file's environment
    /// index; false when the file is unreadable or of another cache version.
    bool LoadPrecompiledInfos(const std::filesystem::path& precompiled,
                              const std::filesystem::path& infos,
                              const VideoCommon::ShaderWarm::PreparedStore& store);

    /// A precompiled pipeline for the current key, drawn with the shader infos the preparation
    /// step kept while a worker builds it from the SPIR-V it kept (or, without it, translates
    /// the recorded environments): the GPU thread checks that the game's state now answers the
    /// recorded environments' questions as they were answered (FileEnvironment::AnswersHoldFor),
    /// so the kept translation is the one the game's state would give, instead of translating.
    /// Null when the key is not precompiled, its answers differ, or its infos are missing.
    std::unique_ptr<GraphicsPipeline> CreatePrecompiledGraphicsPipeline(
        GraphicsEnvironments& environments);

    /// EDEN_NX2_PRECOMPILED_AUDIT=1: a precompiled pipeline just created is also translated from
    /// the game's state, on the GPU thread, and its SPIR-V and infos compared with the kept ones
    /// (counted in the 10 s report, the first differences logged). Slow: a check of the
    /// relational answers (FileEnvironment::AnswersHoldFor), not for play. A compute pipeline
    /// passes the record it was made from.
    void AuditPrecompiled(std::span<Shader::Environment* const> envs, bool compute, u64 key_hash,
                          const PrecompiledRecord* record = nullptr);

    /// The current pipeline was precompiled and its build failed (its translation did not give
    /// the infos it was drawn with): replaced by one translated from the game's state.
    GraphicsPipeline* ReplaceFailedPrecompiled();

    struct PrecompiledRecord {
        std::vector<u8> key;
        std::array<u32, 5> envs{}; ///< The precompiled file's environments, in stage order
        u8 env_count{};
        std::array<u32, 5> infos{}; ///< Per pipeline stage: an info blob, ~0U for none
        std::array<u32, 5> spirv{}; ///< Per pipeline stage: a SPIR-V blob (zstd), ~0U for none
    };
    std::unordered_map<u64, PrecompiledRecord> precompiled_records; ///< By key hash
    /// By key hash; a compute key has a record for each storage pointer state it was prepared in
    std::unordered_multimap<u64, PrecompiledRecord> precompiled_compute_records;
    std::vector<std::pair<u64, u64>> precompiled_env_index;  ///< Offset, size
    std::vector<std::pair<u64, u32>> precompiled_info_blobs; ///< Offset, size (all blobs)
    std::filesystem::path precompiled_path;
    std::filesystem::path precompiled_infos_path;
    std::ifstream precompiled_file;               ///< GPU thread only
    std::ifstream precompiled_infos_file;         ///< GPU thread only
    std::unordered_set<u64> precompiled_rejected; ///< Keys whose precompiled build failed

    /// Native pipelines: the key without state that cannot change the pipeline (see
    /// FixedPipelineState::CanonicalizeNative).
    void CanonicalizeNativeKey(GraphicsPipelineCacheKey& key,
                               std::span<const Shader::Environment* const> envs) const noexcept;

    /// Native vertex programs found to load attributes their header does not list: their keys
    /// keep every attribute type.
    bool IsUnmaskedVertexProgram(u64 hash) const {
        if (!any_unmasked_vertex_program.load(std::memory_order_acquire)) {
            return false;
        }
        std::scoped_lock lock{unmasked_mutex};
        return unmasked_vertex_programs.contains(hash);
    }
    mutable std::mutex unmasked_mutex;
    std::unordered_set<u64> unmasked_vertex_programs;
    std::atomic<bool> any_unmasked_vertex_program{};

    const Device& device;
    Scheduler& scheduler;
    DescriptorPool& descriptor_pool;
    GuestDescriptorQueue& guest_descriptor_queue;
    DescriptorBufferRing& descriptor_buffer_ring;
    RenderPassCache& render_pass_cache;
    BufferCache& buffer_cache;
    TextureCache& texture_cache;
    VideoCore::ShaderNotify& shader_notify;
    bool use_asynchronous_shaders{};
    bool use_vulkan_pipeline_cache{};
    void StartMeshWarm(u64 title_id, bool before_start, std::stop_token stop_loading,
                       const VideoCore::DiskResourceLoadCallback& callback);
    bool mesh_warm_enabled{};
    std::jthread mesh_warm_thread;
    bool use_shader_reuse{};
    ShaderReuseCache shader_reuse;
    const size_t num_pipeline_workers;
    EmissionBudget emission_budget;

    GraphicsPipelineCacheKey graphics_key{};
    /// Native titles: the state as the registers give it. FixedPipelineState refreshes some fields
    /// only when they are dirty, so graphics_key.state is a canonical copy made for each draw.
    FixedPipelineState native_state{};
    GraphicsPipeline* current_pipeline{};

    /// EDEN_NX2_GPL: shader libraries shared by the graphics pipelines. Declared before the
    /// pipelines, so it outlives them.
    GraphicsPipelineLibraryCache graphics_libraries;

    /// Destroys graphics pipelines unused for a while, once the GPU is done with them.
    void EvictUnusedPipelines();
    void LogEvictionPolicy() const;
    /// Built graphics pipelines kept before unused ones are destroyed (0: the old 9,000-frame
    /// eviction, see KeptPipelines). Set at
    /// construction and again once the disk cache has loaded, read on the GPU thread.
    std::atomic<size_t> kept_pipelines{0};
    u64 frame_count{};
    /// Wait budget of native draws for unbuilt pipelines left in this frame, in microseconds
    /// (native_async_draws = Bounded). Only touched on the GPU thread; reset in TickFrame.
    s64 native_wait_budget_us{8000};
    s64 native_wait_used_us{}; ///< Budget spent since the last 10 s log line.
    std::deque<std::pair<u64, std::unique_ptr<GraphicsPipeline>>> evicted_pipelines; ///< (tick, pipeline)
    std::unordered_set<u64> used_and_evicted; ///< Key hashes of pipelines used this session, evicted since

    ::Common::unordered_map<ComputePipelineCacheKey, ComputePipelineCacheEntry> compute_cache;
    /// Replaced compute pipelines, destroyed once the GPU and their queued build are done with
    /// them (tick, pipeline)
    std::deque<std::pair<u64, std::unique_ptr<ComputePipeline>>> retired_compute_pipelines;
    ::Common::unordered_map<GraphicsPipelineCacheKey, std::unique_ptr<GraphicsPipeline>> graphics_cache;

    ShaderPools main_pools;
    std::mutex spare_pools_mutex;
    std::vector<std::shared_ptr<ShaderPools>> spare_pools;

    Shader::Profile profile;
    Shader::HostTranslateInfo host_info;

    std::filesystem::path pipeline_cache_filename;

    /// Key hashes of the graphics pipelines in the disk cache. Only those a recent session used
    /// (used_pipelines) are built at boot; the rest are built when first needed, and not stored
    /// again. Each built pipeline holds driver memory (title 1: 6.5k cached, a quarter used).
    std::unordered_set<u64> disk_pipelines;
    std::unordered_map<u64, u8> used_pipelines; ///< Key hash -> sessions since it was last used
    std::unordered_set<const GraphicsPipeline*> used_this_session;
    std::filesystem::path used_pipelines_filename;
    std::chrono::steady_clock::time_point used_pipelines_saved{};
    void SaveUsedPipelines();

    std::filesystem::path vulkan_pipeline_cache_filename;
    vk::PipelineCache vulkan_pipeline_cache;
    size_t pipelines_since_flush{};
    std::chrono::steady_clock::time_point last_flush{};
    std::atomic<size_t> last_cache_size{};
    std::atomic_bool flush_in_flight{};

    /// Requested first when the cache is destroyed, to stop a rewrite of the disk cache: Android
    /// and yuzu_cmd load with a stop token that never stops
    std::stop_source serialization_stop;
    Common::ThreadWorker workers;
    /// Software HMMA compute pipelines on drivers that take minutes on them
    /// (Device::DefersSoftwareMatrixPipelines); nothing waits for this worker.
    Common::ThreadWorker deferred_workers;
    Common::ThreadWorker serialization_thread;
    DynamicFeatures dynamic_features;
    PipelinePrebuild<GraphicsPipelineCacheKey, GraphicsPipeline> background_pipelines;

    /// Keys whose pipeline is being translated on a translation worker, with the future that
    /// becomes ready when the result waits for adoption (GPU thread only).
    ::Common::unordered_map<GraphicsPipelineCacheKey, std::shared_future<void>>
        translating_graphics;
    /// Finished translations, handed from the translation workers to the GPU thread.
    std::mutex translated_mutex;
    std::vector<TranslatedGraphicsPipeline> translated_graphics;
    std::atomic<size_t> num_translated{};
    /// Keys whose translation needs more than detached environments hold (GPU thread only).
    ::Common::unordered_set<GraphicsPipelineCacheKey> synchronous_graphics;
    /// Set when the cache is destroyed: queued translations return without translating.
    std::atomic_bool stop_translations{};
    /// Zero without asynchronous shaders, or on drivers that break with parallel compiles.
    const size_t num_translation_threads;
    /// Declared last so it is destroyed first: its threads finish the translation they are
    /// running (and drop the queued ones) while everything they use is still alive.
    Common::ThreadWorker translation_workers;
};

} // namespace Vulkan
