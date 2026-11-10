// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <zstd.h>
#include "native_preparation_revision.h"
#include "video_core/shader_warm/prepared_store.h"
#include "video_core/shader_warm/prepared_stage_info.h"
#include <algorithm>
#include <bitset>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <vector>
#include <bit>
#include <numeric>
#include "common/cityhash.h"
#include "common/scm_rev.h"
#include "common/scope_exit.h"
#include "common/fs/fs.h"
#include "common/fs/fs_util.h"
#include "common/fs/path_util.h"
#include "common/memory_detect.h"
#include "common/thread.h"
#include "common/thread_worker.h"
#include "core/core.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/environment.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/ir_opt/passes.h"
#include "video_core/oot_dlss.h"
#include "shader_recompiler/program_header.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/memory_manager.h"
#include "video_core/nx2_perf.h"
#include "video_core/nx2_profile.h"
#include "video_core/nx2_stutter_trace.h"
#include "video_core/pipeline_cache_files.h"
#include "video_core/renderer_vulkan/async_draw_policy.h"
#include "shader_recompiler/info_serialize.h"
#include "common/zstd_compression.h"
#include "video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "video_core/renderer_vulkan/mesh_fallback_limits.h"
#include "video_core/renderer_vulkan/maxwell_to_vk.h"
#include "video_core/renderer_vulkan/pipeline_eviction_policy.h"
#include "video_core/renderer_vulkan/pipeline_helper.h"
#include "video_core/renderer_vulkan/pipeline_shader_emission.h"
#include "video_core/renderer_vulkan/pipeline_statistics.h"
#include "video_core/renderer_vulkan/precache_journal.h"
#include "video_core/renderer_vulkan/vertex_repack.h"
#include "video_core/renderer_vulkan/vk_compute_pipeline.h"
#include "video_core/renderer_vulkan/vk_cooperative_hmma_test.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_pipeline_binaries.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache_records.h"
#include "video_core/renderer_vulkan/vk_pipeline_library.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_cache_v2.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/shader_cache.h"
#include "video_core/shader_cache_v2/cache_file.h"
#include "video_core/shader_cache_v2/records.h"
#include "video_core/shader_environment.h"
#include "video_core/tensor_program_policy.h"
#include "video_core/shader_notify.h"
#include "video_core/shader_warm/mesh_warm_validation.h"
#include "video_core/shader_warm/shader_warm_builder.h"
#include "video_core/spirv_inline_control.h"
#include "video_core/surface.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include "video_core/gpu_logging/gpu_logging.h"

#ifdef __ANDROID__
#include "../../android/app/src/main/jni/android_settings.h"
#endif

namespace Vulkan {

namespace {
using Shader::Backend::SPIRV::EmitSPIRV;
using Shader::Maxwell::ConvertLegacyToGeneric;
using Shader::Maxwell::GenerateGeometryPassthrough;
using Shader::Maxwell::MergeDualVertexPrograms;
using Shader::Maxwell::TranslateProgram;
using VideoCommon::ComputeEnvironment;
using VideoCommon::FileEnvironment;
using VideoCommon::GenericEnvironment;
using VideoCommon::GuessMissing;
using VideoCommon::GraphicsEnvironment;

constexpr u32 CACHE_VERSION = VideoCommon::VulkanPipelineCacheVersion;
size_t KeptPipelines(size_t cached_at_boot);
constexpr size_t VULKAN_CACHE_FLUSH_PIPELINES = 128;
constexpr size_t VULKAN_CACHE_FLUSH_MIN_SECONDS = 30;
constexpr auto& VULKAN_CACHE_MAGIC_NUMBER = VideoCommon::VulkanDriverPipelineCacheMagic;

template <typename Container>
auto MakeSpan(Container& container) {
    return std::span(container.data(), container.size());
}

Shader::OutputTopology MaxwellToOutputTopology(Maxwell::PrimitiveTopology topology) {
    switch (topology) {
    case Maxwell::PrimitiveTopology::Points:
        return Shader::OutputTopology::PointList;
    case Maxwell::PrimitiveTopology::LineStrip:
        return Shader::OutputTopology::LineStrip;
    default:
        return Shader::OutputTopology::TriangleStrip;
    }
}

Shader::CompareFunction MaxwellToCompareFunction(Maxwell::ComparisonOp comparison) {
    switch (comparison) {
    case Maxwell::ComparisonOp::Never_D3D:
    case Maxwell::ComparisonOp::Never_GL:
        return Shader::CompareFunction::Never;
    case Maxwell::ComparisonOp::Less_D3D:
    case Maxwell::ComparisonOp::Less_GL:
        return Shader::CompareFunction::Less;
    case Maxwell::ComparisonOp::Equal_D3D:
    case Maxwell::ComparisonOp::Equal_GL:
        return Shader::CompareFunction::Equal;
    case Maxwell::ComparisonOp::LessEqual_D3D:
    case Maxwell::ComparisonOp::LessEqual_GL:
        return Shader::CompareFunction::LessThanEqual;
    case Maxwell::ComparisonOp::Greater_D3D:
    case Maxwell::ComparisonOp::Greater_GL:
        return Shader::CompareFunction::Greater;
    case Maxwell::ComparisonOp::NotEqual_D3D:
    case Maxwell::ComparisonOp::NotEqual_GL:
        return Shader::CompareFunction::NotEqual;
    case Maxwell::ComparisonOp::GreaterEqual_D3D:
    case Maxwell::ComparisonOp::GreaterEqual_GL:
        return Shader::CompareFunction::GreaterThanEqual;
    case Maxwell::ComparisonOp::Always_D3D:
    case Maxwell::ComparisonOp::Always_GL:
        return Shader::CompareFunction::Always;
    }
    UNIMPLEMENTED_MSG("Unimplemented comparison op={}", comparison);
    return {};
}

Shader::AttributeType CastAttributeType(const FixedPipelineState::VertexAttribute& attr) {
    if (attr.enabled == 0) {
        return Shader::AttributeType::Disabled;
    }
    switch (attr.Type()) {
    case Maxwell::VertexAttribute::Type::UnusedEnumDoNotUseBecauseItWillGoAway:
        ASSERT_MSG(false, "Invalid vertex attribute type!");
        return Shader::AttributeType::Disabled;
    case Maxwell::VertexAttribute::Type::SNorm:
    case Maxwell::VertexAttribute::Type::UNorm:
    case Maxwell::VertexAttribute::Type::Float:
        return Shader::AttributeType::Float;
    case Maxwell::VertexAttribute::Type::SInt:
        return Shader::AttributeType::SignedInt;
    case Maxwell::VertexAttribute::Type::UInt:
        return Shader::AttributeType::UnsignedInt;
    case Maxwell::VertexAttribute::Type::UScaled:
        return Shader::AttributeType::UnsignedScaled;
    case Maxwell::VertexAttribute::Type::SScaled:
        return Shader::AttributeType::SignedScaled;
    }
    return Shader::AttributeType::Float;
}

Shader::AttributeType AttributeType(const FixedPipelineState& state, size_t index) {
    switch (state.DynamicAttributeType(index)) {
    case 0:
        return Shader::AttributeType::Disabled;
    case 1:
        return Shader::AttributeType::Float;
    case 2:
        return Shader::AttributeType::SignedInt;
    case 3:
        return Shader::AttributeType::UnsignedInt;
    }
    return Shader::AttributeType::Disabled;
}

Shader::RuntimeInfo MakeRuntimeInfo(std::span<const Shader::IR::Program> programs,
                                    const GraphicsPipelineCacheKey& key,
                                    const Shader::IR::Program& program,
                                    const Shader::IR::Program* previous_program,
                                    const Vulkan::Device& device) {
    Shader::RuntimeInfo info;
    if (previous_program) {
        info.previous_stage_stores = previous_program->info.stores;
        info.previous_stage_legacy_stores_mapping = previous_program->info.legacy_stores_mapping;
        if (previous_program->is_geometry_passthrough) {
            info.previous_stage_stores.mask |= previous_program->info.passthrough.mask;
        }
    } else {
        info.previous_stage_stores.mask.set();
    }
    if (previous_program && previous_program->stage == Shader::Stage::Mesh) {
        for (const u32 line : previous_program->mesh.primitive_lines) {
            const auto attribute{static_cast<Shader::IR::Attribute>(line)};
            if (Shader::IR::IsGeneric(attribute)) {
                info.per_primitive_generics.set(Shader::IR::GenericAttributeIndex(attribute));
            }
        }
    }
    const Shader::Stage stage{program.stage};
    const bool has_geometry{key.unique_hashes[4] != 0 && !programs[4].is_geometry_passthrough};
    const bool gl_ndc{key.state.ndc_minus_one_to_one != 0};
    const float point_size{std::bit_cast<float>(key.state.point_size)};
    switch (stage) {
    case Shader::Stage::VertexB:
        if (!has_geometry) {
            if (key.state.topology == Maxwell::PrimitiveTopology::Points) {
                info.fixed_state_point_size = point_size;
            }
            if (key.state.xfb_enabled) {
                if (device.IsExtTransformFeedbackSupported()) {
                    auto [varyings, count] =
                        VideoCommon::MakeTransformFeedbackVaryings(key.state.xfb_state);
                    info.xfb_varyings = varyings;
                    info.xfb_count = count;
                } else {
                    LOG_WARNING(Render_Vulkan, "XFB requested in pipeline key but device lacks VK_EXT_transform_feedback; ignoring XFB decorations");
                }
            }
            info.convert_depth_mode = gl_ndc;
        }
        if (key.state.dynamic_vertex_input) {
            for (size_t index = 0; index < Maxwell::NumVertexAttributes; ++index) {
                info.generic_input_types[index] = AttributeType(key.state, index);
            }
        } else {
            std::ranges::transform(key.state.attributes, info.generic_input_types.begin(),
                                   &CastAttributeType);
        }
        break;
    case Shader::Stage::TessellationEval:
        info.tess_clockwise = key.state.tessellation_clockwise != 0;
        info.tess_primitive = [&key] {
            const u32 raw{key.state.tessellation_primitive.Value()};
            switch (static_cast<Maxwell::Tessellation::DomainType>(raw)) {
            case Maxwell::Tessellation::DomainType::Isolines:
                return Shader::TessPrimitive::Isolines;
            case Maxwell::Tessellation::DomainType::Triangles:
                return Shader::TessPrimitive::Triangles;
            case Maxwell::Tessellation::DomainType::Quads:
                return Shader::TessPrimitive::Quads;
            }
            ASSERT(false);
            return Shader::TessPrimitive::Triangles;
        }();
        info.tess_spacing = [&] {
            const u32 raw{key.state.tessellation_spacing};
            switch (static_cast<Maxwell::Tessellation::Spacing>(raw)) {
            case Maxwell::Tessellation::Spacing::Integer:
                return Shader::TessSpacing::Equal;
            case Maxwell::Tessellation::Spacing::FractionalOdd:
                return Shader::TessSpacing::FractionalOdd;
            case Maxwell::Tessellation::Spacing::FractionalEven:
                return Shader::TessSpacing::FractionalEven;
            }
            ASSERT(false);
            return Shader::TessSpacing::Equal;
        }();
        break;
    case Shader::Stage::Mesh:
        info.convert_depth_mode = gl_ndc;
        if (program.mesh.topology == 0) {
            info.fixed_state_point_size = point_size;
        }
        break;
    case Shader::Stage::Geometry:
        if (program.output_topology == Shader::OutputTopology::PointList) {
            info.fixed_state_point_size = point_size;
        }
        if (key.state.xfb_enabled != 0) {
            if (device.IsExtTransformFeedbackSupported()) {
                auto [varyings, count] =
                    VideoCommon::MakeTransformFeedbackVaryings(key.state.xfb_state);
                info.xfb_varyings = varyings;
                info.xfb_count = count;
            } else {
                LOG_WARNING(Render_Vulkan, "XFB requested in pipeline key but device lacks VK_EXT_transform_feedback; ignoring XFB decorations");
            }
        }
        info.convert_depth_mode = gl_ndc;
        break;
    case Shader::Stage::Fragment: {
        info.alpha_test_func = MaxwellToCompareFunction(
            key.state.UnpackComparisonOp(key.state.alpha_test_func.Value()));
        info.alpha_test_reference = std::bit_cast<float>(key.state.alpha_test_ref);

        info.dual_source_blend = key.state.attachment0_dual_source_blend != 0;

        if (device.IsMoltenVK()) {
            for (size_t i = 0; i < 8; ++i) {
                const auto format = static_cast<Tegra::RenderTargetFormat>(key.state.color_formats[i]);
                if (format == Tegra::RenderTargetFormat::NONE) {
                    info.color_output_types[i] = Shader::AttributeType::Float;
                    continue;
                }
                const auto pixel_format = VideoCore::Surface::PixelFormatFromRenderTargetFormat(format);
                if (VideoCore::Surface::IsPixelFormatInteger(pixel_format)) {
                    if (VideoCore::Surface::IsPixelFormatSignedInteger(pixel_format)) {
                        info.color_output_types[i] = Shader::AttributeType::SignedInt;
                    } else {
                        info.color_output_types[i] = Shader::AttributeType::UnsignedInt;
                    }
                } else {
                    info.color_output_types[i] = Shader::AttributeType::Float;
                }
            }
        }
        break;
    }
    default:
        break;
    }
    switch (key.state.topology) {
    case Maxwell::PrimitiveTopology::Points:
        info.input_topology = Shader::InputTopology::Points;
        break;
    case Maxwell::PrimitiveTopology::Lines:
    case Maxwell::PrimitiveTopology::LineLoop:
    case Maxwell::PrimitiveTopology::LineStrip:
        info.input_topology = Shader::InputTopology::Lines;
        break;
    case Maxwell::PrimitiveTopology::Triangles:
    case Maxwell::PrimitiveTopology::TriangleStrip:
    case Maxwell::PrimitiveTopology::TriangleFan:
    case Maxwell::PrimitiveTopology::Quads:
    case Maxwell::PrimitiveTopology::QuadStrip:
    case Maxwell::PrimitiveTopology::Polygon:
    case Maxwell::PrimitiveTopology::Patches:
        info.input_topology = Shader::InputTopology::Triangles;
        break;
    case Maxwell::PrimitiveTopology::LinesAdjacency:
    case Maxwell::PrimitiveTopology::LineStripAdjacency:
        info.input_topology = Shader::InputTopology::LinesAdjacency;
        break;
    case Maxwell::PrimitiveTopology::TrianglesAdjacency:
    case Maxwell::PrimitiveTopology::TriangleStripAdjacency:
        info.input_topology = Shader::InputTopology::TrianglesAdjacency;
        break;
    }
    info.force_early_z = key.state.early_z != 0;
    if (program.info.oot_motion.early_tests &&
        (!info.alpha_test_func || *info.alpha_test_func == Shader::CompareFunction::Always)) {
        info.force_early_z = true;
    }
    info.y_negate = key.state.y_negate != 0;
    // Transform feedback would capture the scaled values, a passthrough geometry shader doesn't
    // scale the outputs it writes itself, and indexed attribute reads in the fragment stage don't
    // undo the scaling, so all three keep native noperspective interpolation.
    const bool geometry_passthrough{key.unique_hashes[4] != 0 &&
                                    programs[4].is_geometry_passthrough};
    if (key.unique_hashes[5] != 0 && !geometry_passthrough && key.state.xfb_enabled == 0 &&
        !programs[5].info.loads_indexed_attributes) {
        const bool has_tess{key.unique_hashes[3] != 0};
        const bool last_pre_raster{
            (stage == Shader::Stage::VertexB && !has_tess && !has_geometry) ||
            (stage == Shader::Stage::TessellationEval && !has_geometry) ||
            (stage == Shader::Stage::Geometry && has_geometry)};
        if (stage == Shader::Stage::Fragment) {
            info.scaled_noperspective_inputs =
                previous_program && (previous_program->stage == Shader::Stage::VertexB ||
                                     previous_program->stage == Shader::Stage::TessellationEval ||
                                     previous_program->stage == Shader::Stage::Geometry);
        }
        if (last_pre_raster) {
            const auto& fragment_info{programs[5].info};
            for (size_t index = 0; index < 32; ++index) {
                if (fragment_info.interpolation[index] == Shader::Interpolation::NoPerspective) {
                    info.noperspective_outputs.set(index);
                }
            }
        }
    }
    return info;
}

/// SPIR-V emission of run-time pipelines: graphics on the GPU thread, graphics on workers and
/// compute on workers (shader cache v2)
std::array<std::atomic<std::chrono::nanoseconds::rep>, 3> runtime_spirv_ns{};

/// EDEN_NX2_HEAVY_SKIP counters for the 10 s report (GPU thread only): draws skipped, pipelines
/// that had draws skipped, and heavy unbuilt draws held instead because the pipeline writes
/// storage or the render target is not redrawn every frame.
struct HeavySkipCounters {
    u32 draws{};
    u32 pipelines{};
    u32 held_storage{};
    u32 held_not_cleared{};
    u32 held_new_target{};
};
HeavySkipCounters heavy_skip_counters;

/// Runtime pipelines built from precompiled infos, and those whose recorded answers differed
/// (by the first answer that did), since the last 10 s report.
std::atomic<u32> precompiled_drawn{};
std::atomic<u32> precompiled_failed{};
std::atomic<u32> precompiled_created{};
std::atomic<u32> precompiled_from_spirv{};
std::atomic<u32> precompiled_per_stage{};
std::atomic<std::chrono::nanoseconds::rep> precompiled_gpu_ns{};
std::mutex precompiled_reasons_mutex;
std::map<std::string, u32> precompiled_reasons;

void CountPrecompiledReason(std::string_view reason) {
    std::scoped_lock lock{precompiled_reasons_mutex};
    ++precompiled_reasons[std::string{reason}];
}

/// Most recorded environments a stage's lookup checks against the game's state: a program kept
/// in many environments (other texture types, pointer states) is checked against the first ones.
constexpr size_t MaxStageCandidates = 16;

/// The pixel format traits a translation used: integer formats always (sampled textures), the
/// others only with descriptors that use them (FileEnvironment::AnswersHoldFor).
u32 UsedFormatTraits(const Shader::Info& info) {
    u32 traits = FileEnvironment::FormatTraitInteger;
    if (std::ranges::any_of(info.image_descriptors,
                            [](const auto& desc) { return desc.is_byte_addressed; })) {
        traits |= FileEnvironment::FormatTraitByteAddressed;
    }
    if (!info.texture_buffer_descriptors.empty()) {
        traits |= FileEnvironment::FormatTraitSnorm;
    }
    return traits;
}

/// EDEN_NX2_PRECOMPILED_FAST=0: precompiled pipelines are translated on the GPU thread like the
/// others (their kept infos and SPIR-V unused), to compare.
bool PrecompiledFastPath() {
    static const bool enabled = [] {
        const char* value = std::getenv("EDEN_NX2_PRECOMPILED_FAST");
        const bool on = !(value && value[0] == '0');
        if (!on) {
            LOG_INFO(Render_Vulkan, "EDEN_NX2_PRECOMPILED_FAST=0: precompiled pipelines are "
                                    "translated at runtime");
        }
        return on;
    }();
    return enabled;
}

bool PrecompiledAudit() {
    static const bool enabled = [] {
        const char* value = std::getenv("EDEN_NX2_PRECOMPILED_AUDIT");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}
std::atomic<u32> audit_same{};
std::atomic<u32> audit_differ{};

/// The words of a kept SPIR-V blob, on the GPU thread.
std::vector<u32> ReadKeptSpirv(std::ifstream& file, std::pair<u64, u32> blob) {
    const auto compressed =
        VideoCommon::ShaderWarm::PreparedStore::ReadBlob(file, {blob.first, blob.second});
    const auto size = ZSTD_getFrameContentSize(compressed.data(), compressed.size());
    if (size < 20 || size > VideoCommon::ShaderWarm::PreparedStore::MaxPayloadBytes ||
        size % 4 != 0) {
        throw Shader::LogicError("Invalid prepared SPIR-V size");
    }
    std::vector<u32> words(size / sizeof(u32));
    if (ZSTD_decompress(words.data(), size, compressed.data(), compressed.size()) != size ||
        words[0] != 0x07230203 || words[4] != 0) {
        throw Shader::LogicError("Invalid prepared SPIR-V");
    }
    return words;
}

vk::ShaderModule BuildKeptSpirv(const Device& device, const std::filesystem::path& path,
                                std::pair<u64, u32> blob, [[maybe_unused]] u64 pipeline_hash) {
    std::ifstream file(path, std::ios::binary);
    return BuildShader(device, ReadKeptSpirv(file, blob));
}
constexpr u32 NoInfo = ~0U;

void AddRuntimeSpirvTime(size_t thread, std::chrono::steady_clock::time_point begin) {
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    runtime_spirv_ns[thread].fetch_add(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count(),
        std::memory_order::relaxed);
}

// EDEN_NX2_GRAPHICS_NOINLINE=0 turns it off: graphics modules are then built from the SPIR-V
// exactly as emitted. On by default: functions a graphics stage calls from several places (the
// global memory functions of native programs) are built as calls instead of being inlined at
// each call site, as for compute (EDEN_NX2_COMPUTE_NOINLINE, video_core/spirv_inline_control.h).
bool GraphicsNoInline() {
    // The environment variable wins over the setting (shader_calls_not_inlined).
    static const int forced = [] {
        const char* value = std::getenv("EDEN_NX2_GRAPHICS_NOINLINE");
        return value == nullptr || *value == '\0' ? -1 : (value[0] == '0' ? 0 : 1);
    }();
    return forced >= 0 ? forced == 1 : Settings::values.shader_calls_not_inlined.GetValue();
}

vk::ShaderModule EmitShaderModule(const Device& device, const Shader::Profile& profile,
                                 const Shader::RuntimeInfo& runtime_info,
                                 Shader::IR::Program& program, Shader::Backend::Bindings& binding,
                                 u64 shader_hash, size_t stage_index,
                                 std::span<const u32> reference = {},
                                 u128* spirv_hash = nullptr,
                                 std::vector<u32>* code_out = nullptr) {
    const auto code =
        EmitPipelineShader(profile, runtime_info, program, binding, shader_hash, reference);
    // Every graphics module (monolithic pipelines, pipeline libraries, the graphics warm-up, on
    // the GPU thread or on a worker) is made here, so the pass runs once on every path.
    std::vector<u32> module_code(code.begin(), code.end());
    if (GraphicsNoInline()) {
        if (const size_t marked = VideoCommon::MarkSharedFunctionsDontInline(module_code);
            marked != 0) {
            static std::atomic<u32> logged{};
            if (logged.fetch_add(1, std::memory_order_relaxed) < 32) {
                LOG_INFO(Render_Vulkan,
                         "Graphics shader {:016x}: {} shared functions built as calls, not "
                         "inlined",
                         shader_hash, marked);
            }
        }
    }
    if (spirv_hash) {
        // Graphics pipeline libraries are keyed by the SPIR-V handed to the driver (after the
        // DontInline pass), so a library is only reused for the exact same module words.
        *spirv_hash = Common::CityHash128(reinterpret_cast<const char*>(module_code.data()),
                                          module_code.size() * sizeof(u32));
    }
    if (code_out) {
        *code_out = module_code;
    }
    device.SaveShader(code);
    vk::ShaderModule module{BuildShader(device, module_code)};

    // Text log + .spv dump. Text log is gated by gpu_log_level != Off; .spv dump
    // is independent and gated only by gpu_log_shader_dumps.
    const bool should_log = GPU::Logging::IsActive();
    const bool should_dump = Settings::values.gpu_log_shader_dumps.GetValue();
    if (should_log || should_dump) {
        static constexpr std::array stage_names{"vertex", "tess_control", "tess_eval", "geometry",
                                               "fragment"};
        const std::string shader_name =
            fmt::format("shader_{:016x}_{}", shader_hash, stage_names[stage_index]);
        if (should_log) {
            const std::string shader_info = fmt::format(
                "SPIR-V size: {} bytes, hash: {:016x}", code.size() * sizeof(u32), shader_hash);
            GPU::Logging::GPULogger::GetInstance().LogShaderCompilation(shader_name, shader_info);
        }
        if (should_dump) {
            GPU::Logging::DumpSpirvShader(shader_hash,
                                         std::span<const u32>(code.data(), code.size()));
        }
    }

    if (device.HasDebuggingToolAttached()) {
        const std::string name{fmt::format("Shader {:016x}", shader_hash)};
        module.SetObjectNameEXT(name.c_str());
    }
    return module;
}

// Graphics pipeline libraries (EDEN_NX2_GPL): each stage's resources go to its own descriptor
// set (set index = stage index) with bindings from 0, and the fragment stage's rescaling bits
// start at fixed indices, so a stage's SPIR-V does not depend on the other stages' resources.
void PrepareStageBindings(Shader::Backend::Bindings& binding, size_t stage_index, bool libraries) {
    if (!libraries) {
        return;
    }
    binding.unified = 0;
    binding.uniform_buffer = 0;
    binding.storage_buffer = 0;
    binding.texture = 0;
    binding.image = 0;
    binding.descriptor_set = static_cast<u32>(stage_index);
    if (stage_index + 1 == Maxwell::MaxShaderStage) {
        binding.texture_scaling_index = GPL_FRAGMENT_TEXTURE_SCALING_BASE;
        binding.image_scaling_index = GPL_FRAGMENT_IMAGE_SCALING_BASE;
    }
}

// EDEN_NX2_COMPUTE_SPIRV_DUMP=<dir>: write each compute shader's SPIR-V the first time a pipeline is
// made from it, as <dir>/<shader hash>.spv (to time driver compiles outside the emulator).
void DumpComputeSpirv(u64 shader_hash, std::span<const u32> code) {
    static const char* const directory = std::getenv("EDEN_NX2_COMPUTE_SPIRV_DUMP");
    if (directory == nullptr || directory[0] == '\0') {
        return;
    }
    const auto path = std::filesystem::path{directory} / fmt::format("{:016x}.spv", shader_hash);
    std::ofstream file{path, std::ios::binary};
    file.write(reinterpret_cast<const char*>(code.data()),
               static_cast<std::streamsize>(code.size_bytes()));
}

// EDEN_NX2_COMPUTE_NOINLINE=0 turns it off: compute modules are then built from the SPIR-V exactly
// as emitted. On by default: functions a compute shader calls from several places (the global
// memory functions of native programs) are built as calls instead of being inlined at each call
// site, which made NVIDIA spend 2-13 s on single compute shaders of a test title, every one a
// freeze at its first dispatch (video_core/spirv_inline_control.h).
bool ComputeNoInline() {
    // The environment variable wins over the setting (shader_calls_not_inlined).
    static const int forced = [] {
        const char* value = std::getenv("EDEN_NX2_COMPUTE_NOINLINE");
        return value == nullptr || *value == '\0' ? -1 : (value[0] == '0' ? 0 : 1);
    }();
    return forced >= 0 ? forced == 1 : Settings::values.shader_calls_not_inlined.GetValue();
}

// The words a compute module is built from. The translation cache keeps the SPIR-V as emitted,
// so cached and freshly translated stages are built alike.
std::vector<u32> ComputeModuleCode(std::span<const u32> code, u64 shader_hash) {
    DumpComputeSpirv(shader_hash, code);
    std::vector<u32> words(code.begin(), code.end());
    if (ComputeNoInline()) {
        if (const size_t marked = VideoCommon::MarkSharedFunctionsDontInline(words); marked != 0) {
            static std::atomic<u32> logged{};
            if (logged.fetch_add(1, std::memory_order_relaxed) < 32) {
                LOG_INFO(Render_Vulkan,
                         "Compute shader {:016x}: {} shared functions built as calls, not inlined",
                         shader_hash, marked);
            }
        }
    }
    return words;
}

vk::ShaderModule EmitComputeModule(const Device& device, const Shader::Profile& profile,
                                   Shader::IR::Program& program, u64 shader_hash,
                                   std::span<const u32> reference = {},
                                   std::vector<u32>* code_out = nullptr) {
    const std::vector<u32> code{EmitComputeShader(profile, program, shader_hash, reference)};
    const auto module_code = ComputeModuleCode(code, shader_hash);
    if (code_out) {
        *code_out = module_code;
    }
    device.SaveShader(code);
    // Saved as emitted, built from ComputeModuleCode's words (NoInline pass, SPIR-V dump)
    vk::ShaderModule spv_module{BuildShader(device, module_code)};

    // Text log + .spv dump. Same split as the graphics path.
    const bool should_log = GPU::Logging::IsActive();
    const bool should_dump = Settings::values.gpu_log_shader_dumps.GetValue();
    if (should_log || should_dump) {
        const std::string shader_name = fmt::format("shader_{:016x}_compute", shader_hash);
        if (should_log) {
            const std::string shader_info = fmt::format("SPIR-V size: {} bytes, hash: {:016x}",
                code.size() * sizeof(u32), shader_hash);
            GPU::Logging::GPULogger::GetInstance().LogShaderCompilation(shader_name, shader_info);
        }
        if (should_dump) {
            GPU::Logging::DumpSpirvShader(shader_hash,
                                          std::span<const u32>(code.data(), code.size()));
        }
    }

    if (device.HasDebuggingToolAttached()) {
        const auto name{fmt::format("Shader {:016x}", shader_hash)};
        spv_module.SetObjectNameEXT(name.c_str());
    }
    return spv_module;
}

/// A compute program whose SPIR-V the CPU stage of its pipeline's job emits (shader cache v2)
struct DeferredCompute {
    std::unique_ptr<QueuedIrBudget::Reservation> reservation;
    std::shared_ptr<ShaderPools> pools;
    Shader::IR::Program program;
    std::vector<u32> reference_spirv;
};

struct DeferredStages {
    std::unique_ptr<QueuedIrBudget::Reservation> reservation;
    std::shared_ptr<ShaderPools> pools;
    /// Pools of the stages translated on workers (ParallelTranslation)
    std::vector<std::shared_ptr<ShaderPools>> stage_pools;
    std::array<Shader::IR::Program, Maxwell::MaxShaderProgram> programs;
    std::array<std::optional<Shader::RuntimeInfo>, Maxwell::MaxShaderStage> runtime_infos;
    std::array<std::vector<u32>, Maxwell::MaxShaderStage> reference_spirv;
};

size_t GetTotalPipelineWorkers() {
    const size_t max_core_threads =
        std::max<size_t>(static_cast<size_t>(std::thread::hardware_concurrency()), 2ULL) - 1ULL;
#ifdef __ANDROID__
    const int configured = AndroidSettings::values.pipeline_worker_count.GetValue();
    const int clamped = std::clamp(configured, 2, 8);
    const size_t desired = static_cast<size_t>(clamped);
    if (desired == 0) {
        return 1ULL;
    }
    return std::min(max_core_threads, desired);
#else
    return max_core_threads;
#endif
}

} // Anonymous namespace

struct PipelineCache::GraphicsStages {
    std::array<Shader::IR::Program, Maxwell::MaxShaderProgram> programs;
    /// Per pipeline stage (vertex B to fragment), for the stages the pipeline has.
    std::array<std::optional<Shader::RuntimeInfo>, Maxwell::MaxShaderStage> runtime_infos;
    std::array<std::vector<u32>, Maxwell::MaxShaderStage> reference_spirv;
    std::optional<MeshEmulationSetup> mesh_emulation;
    bool use_libraries{};
    size_t source_words{};
};

size_t ComputePipelineCacheKey::Hash() const noexcept {
    const u64 hash = Common::CityHash64(reinterpret_cast<const char*>(this), sizeof *this);
    return static_cast<size_t>(hash);
}

bool ComputePipelineCacheKey::operator==(const ComputePipelineCacheKey& rhs) const noexcept {
    return std::memcmp(&rhs, this, sizeof *this) == 0;
}

size_t GraphicsPipelineCacheKey::Hash() const noexcept {
    const u64 hash = Common::CityHash64(reinterpret_cast<const char*>(this), Size());
    return static_cast<size_t>(hash);
}

bool GraphicsPipelineCacheKey::operator==(const GraphicsPipelineCacheKey& rhs) const noexcept {
    if (unique_hashes != rhs.unique_hashes) {
        return false;
    }
    return std::memcmp(reinterpret_cast<const char*>(this) + sizeof(unique_hashes),
                       reinterpret_cast<const char*>(&rhs) + sizeof(unique_hashes),
                       state.Size()) == 0;
}

// Builds the shader profile from the device, including which cooperative HMMA path runs.
PipelineCache::PipelineCache(Tegra::MaxwellDeviceMemoryManager& device_memory_,
                             const Device& device_, Scheduler& scheduler_,
                             DescriptorPool& descriptor_pool_,
                             GuestDescriptorQueue& guest_descriptor_queue_,
                             DescriptorBufferRing& descriptor_buffer_ring_,
                             RenderPassCache& render_pass_cache_, BufferCache& buffer_cache_,
                             TextureCache& texture_cache_, VideoCore::ShaderNotify& shader_notify_)
    : VideoCommon::ShaderCache{device_memory_}, device{device_}, scheduler{scheduler_},
      descriptor_pool{descriptor_pool_}, guest_descriptor_queue{guest_descriptor_queue_},
      descriptor_buffer_ring{descriptor_buffer_ring_}, render_pass_cache{render_pass_cache_},
      buffer_cache{buffer_cache_}, texture_cache{texture_cache_}, shader_notify{shader_notify_},
      use_asynchronous_shaders{Settings::values.use_asynchronous_shaders.GetValue()},
      use_vulkan_pipeline_cache{Settings::values.use_vulkan_driver_pipeline_cache.GetValue() &&
                                !device_.DriverCachesPipelines()},
      mesh_warm_enabled{Settings::values.use_mesh_shader_warmup.GetValue()},
      serialization_thread(1, "VkPipelineSerialization", {}, Common::ThreadPlacement::Background) {
    if (Settings::values.use_vulkan_driver_pipeline_cache.GetValue() && !use_vulkan_pipeline_cache) {
        LOG_INFO(Render_Vulkan, "Driver keeps its own pipeline cache, not saving a Vulkan one");
    }
    const auto& float_control{device.FloatControlProperties()};
    const VkDriverId driver_id{device.GetDriverID()};
    const VkShaderStageFlags subgroup_stages{device.GetSubgroupSupportedStages()};
    const auto subgroup_stage_bit{[subgroup_stages](VkShaderStageFlags flag, Shader::Stage stage) {
        return (subgroup_stages & flag) != 0 ? (1u << static_cast<u32>(stage)) : 0u;
    }};
    u32 supported_subgroup_stages{
        subgroup_stage_bit(VK_SHADER_STAGE_VERTEX_BIT, Shader::Stage::VertexA) |
        subgroup_stage_bit(VK_SHADER_STAGE_VERTEX_BIT, Shader::Stage::VertexB) |
        subgroup_stage_bit(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                           Shader::Stage::TessellationControl) |
        subgroup_stage_bit(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                           Shader::Stage::TessellationEval) |
        subgroup_stage_bit(VK_SHADER_STAGE_GEOMETRY_BIT, Shader::Stage::Geometry) |
        subgroup_stage_bit(VK_SHADER_STAGE_FRAGMENT_BIT, Shader::Stage::Fragment) |
        subgroup_stage_bit(VK_SHADER_STAGE_COMPUTE_BIT, Shader::Stage::Compute) |
        subgroup_stage_bit(VK_SHADER_STAGE_TASK_BIT_EXT, Shader::Stage::Task) |
        subgroup_stage_bit(VK_SHADER_STAGE_MESH_BIT_EXT, Shader::Stage::Mesh)};
    if (driver_id == VK_DRIVER_ID_MESA_TURNIP) {
        supported_subgroup_stages &= ~(1u << static_cast<u32>(Shader::Stage::Geometry));
    }
    profile = Shader::Profile{
        .supported_spirv = device.SupportedSpirvVersion(),
        .unified_descriptor_binding = true,
        .support_descriptor_aliasing = device.IsDescriptorAliasingSupported(),
        .support_int8 = device.IsInt8Supported(),
        .support_uniform_and_storage_buffer_8bit =
            device.IsUniformAndStorageBuffer8BitAccessSupported(),
        .support_storage_buffer_8bit = device.IsStorageBuffer8BitAccessSupported(),
        .support_int16 = device.IsShaderInt16Supported(),
        .support_uniform_and_storage_buffer_16bit =
            device.IsUniformAndStorageBuffer16BitAccessSupported(),
        .support_storage_buffer_16bit = device.IsStorageBuffer16BitAccessSupported(),
        .support_int64 = device.IsShaderInt64Supported(),
        .support_vertex_instance_id = false,
        .support_float_controls = device.IsKhrShaderFloatControlsSupported(),
        .support_separate_denorm_behavior =
            float_control.denormBehaviorIndependence == VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL,
        .support_separate_rounding_mode =
            float_control.roundingModeIndependence == VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL,
        .support_fp16_denorm_preserve = float_control.shaderDenormPreserveFloat16 != VK_FALSE,
        .support_fp32_denorm_preserve = float_control.shaderDenormPreserveFloat32 != VK_FALSE,
        .support_fp16_denorm_flush = float_control.shaderDenormFlushToZeroFloat16 != VK_FALSE,
        .support_fp32_denorm_flush = float_control.shaderDenormFlushToZeroFloat32 != VK_FALSE,
        .support_fp16_signed_zero_nan_preserve =
            float_control.shaderSignedZeroInfNanPreserveFloat16 != VK_FALSE,
        .support_fp32_signed_zero_nan_preserve =
            float_control.shaderSignedZeroInfNanPreserveFloat32 != VK_FALSE,
        .support_fp64_signed_zero_nan_preserve =
            float_control.shaderSignedZeroInfNanPreserveFloat64 != VK_FALSE,
        .support_explicit_workgroup_layout = device.IsKhrWorkgroupMemoryExplicitLayoutSupported(),
        .support_workgroup_layout_8bit_access =
            device.IsWorkgroupMemoryExplicitLayout8BitAccessSupported(),
        .support_workgroup_layout_16bit_access =
            device.IsWorkgroupMemoryExplicitLayout16BitAccessSupported(),
        .support_shader_quad_control = device.IsKhrShaderQuadControlSupported(),
        .support_quad_shuffles = device.IsSubgroupFeatureSupported(VK_SUBGROUP_FEATURE_QUAD_BIT),
        .support_vote = device.IsSubgroupFeatureSupported(VK_SUBGROUP_FEATURE_VOTE_BIT),
        .support_shuffle_relative =
            device.IsSubgroupFeatureSupported(VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT),
        .supported_subgroup_stages = supported_subgroup_stages,
        .support_viewport_index_layer_non_geometry =
            device.IsExtShaderViewportIndexLayerSupported(),
        .support_viewport_mask = device.IsNvViewportArray2Supported(),
        .support_typeless_image_loads = device.IsFormatlessImageLoadSupported(),
        .support_demote_to_helper_invocation =
            device.IsExtShaderDemoteToHelperInvocationSupported(),
        .support_int64_atomics = device.IsExtShaderAtomicInt64Supported(),
        .support_shared_int64_atomics = device.IsSharedInt64AtomicsSupported(),
        .support_derivative_control = true,
        .support_geometry_shader_passthrough = device.IsNvGeometryShaderPassthroughSupported(),
        .support_native_ndc = device.IsExtDepthClipControlSupported(),
        .support_scaled_attributes = !device.MustEmulateScaledFormats(),
        .support_multi_viewport = device.SupportsMultiViewport(),
        .support_geometry_streams = device.AreTransformFeedbackGeometryStreamsSupported(),
        .support_sampled_image_array_nonuniform_indexing =
            device.IsSampledImageArrayNonUniformIndexingSupported(),
        .support_storage_image_array_nonuniform_indexing =
            device.IsStorageImageArrayNonUniformIndexingSupported(),
        .support_uniform_texel_buffer_array_nonuniform_indexing =
            device.IsUniformTexelBufferArrayNonUniformIndexingSupported(),
        .support_storage_texel_buffer_array_nonuniform_indexing =
            device.IsStorageTexelBufferArrayNonUniformIndexingSupported(),

        .warp_size_potentially_larger_than_guest = device.IsWarpSizePotentiallyBiggerThanGuest(),

        .lower_left_origin_mode = false,
        .need_declared_frag_colors = false,
        .need_gather_subpixel_offset = driver_id == VK_DRIVER_ID_AMD_PROPRIETARY ||
                                       driver_id == VK_DRIVER_ID_AMD_OPEN_SOURCE ||
                                       driver_id == VK_DRIVER_ID_MESA_RADV ||
                                       driver_id == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS ||
                                       driver_id == VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA ||
                                       driver_id == VK_DRIVER_ID_QUALCOMM_PROPRIETARY ||
                                       driver_id == VK_DRIVER_ID_MESA_TURNIP,

        .has_broken_spirv_clamp = driver_id == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS,
        .has_broken_spirv_position_input = driver_id == VK_DRIVER_ID_QUALCOMM_PROPRIETARY,
        .has_broken_unsigned_image_offsets = false,
        .has_broken_signed_operations = false,
        .has_broken_fp16_float_controls = driver_id == VK_DRIVER_ID_NVIDIA_PROPRIETARY,
        .max_compute_shared_memory_size = device.GetMaxComputeSharedMemorySize(),
        .has_broken_fp32_denorm_flush = driver_id == VK_DRIVER_ID_QUALCOMM_PROPRIETARY,
        .ignore_nan_fp_comparisons = false,
        .has_broken_spirv_subgroup_mask_vector_extract_dynamic =
            driver_id == VK_DRIVER_ID_QUALCOMM_PROPRIETARY &&
            device.GetDriverVersion() < VK_MAKE_VERSION(512, 672, 0),
        .has_broken_spirv_subgroup_shuffle = driver_id == VK_DRIVER_ID_QUALCOMM_PROPRIETARY,
        .has_broken_spirv_fragment_shuffle = driver_id == VK_DRIVER_ID_AMD_PROPRIETARY,
        .max_subgroup_size = device.GetMaxSubgroupSize(),
        .has_broken_robust =
            device.IsNvidia() && device.GetNvidiaArch() <= NvidiaArchitecture::Arch_Pascal,
        .min_ssbo_alignment = device.GetStorageBufferAlignment(),
        .max_user_clip_distances = device.GetMaxUserClipDistances()
    };
    profile.support_cooperative_hmma =
        device.IsCooperativeHmmaSupported() && TestCooperativeHmma(device);
    profile.support_cooperative_hmma_16x16x16 =
        !profile.support_cooperative_hmma && device.IsCooperativeHmma16x16x16Supported();
    profile.support_cooperative_hmma_8x16x16 = !profile.support_cooperative_hmma &&
                                               !profile.support_cooperative_hmma_16x16x16 &&
                                               device.IsCooperativeHmma8x16x16Supported();
    if (profile.support_cooperative_hmma_16x16x16) {
        LOG_INFO(Render_Vulkan, "Guest tensor operations run on 16x16x16 cooperative matrices");
    } else if (profile.support_cooperative_hmma_8x16x16) {
        LOG_INFO(Render_Vulkan, "Guest tensor operations run on 8x16x16 cooperative matrices");
    }

    host_info = Shader::HostTranslateInfo{
        .min_ssbo_alignment = device.GetStorageBufferAlignment(),
        .max_per_stage_descriptor_sampled_images = device.GetMaxPerStageDescriptorSampledImages(),
        .max_per_stage_resources = device.GetMaxPerStageResources(),
        .max_per_stage_descriptor_storage_buffers = device.GetMaxPerStageDescriptorStorageBuffers(),
        .max_per_stage_descriptor_samplers = device.GetMaxPerStageDescriptorSamplers(),
        .max_descriptor_set_samplers = device.GetMaxDescriptorSetSamplers(),
        .max_descriptor_set_uniform_buffers = device.GetMaxDescriptorSetUniformBuffers(),
        .max_descriptor_set_uniform_buffers_dynamic = device.GetMaxDescriptorSetUniformBuffersDynamic(),
        .max_descriptor_set_storage_buffers = device.GetMaxDescriptorSetStorageBuffers(),
        .max_descriptor_set_storage_buffers_dynamic = device.GetMaxDescriptorSetStorageBuffersDynamic(),
        .max_descriptor_set_sampled_images = device.GetMaxDescriptorSetSampledImages(),
        .max_descriptor_set_storage_images = device.GetMaxDescriptorSetStorageImages(),
        .max_descriptor_set_input_attachements = device.GetMaxDescriptorSetInputAttachments(),
        .support_float64 = device.IsFloat64Supported(),
        .support_float16 = device.IsFloat16Supported(),
        .support_int64 = device.IsShaderInt64Supported(),
        .needs_demote_reorder = driver_id == VK_DRIVER_ID_AMD_PROPRIETARY ||
                                driver_id == VK_DRIVER_ID_AMD_OPEN_SOURCE ||
                                driver_id == VK_DRIVER_ID_SAMSUNG_PROPRIETARY,
        .support_snorm_render_buffer = true,
        .support_viewport_index_layer = device.IsExtShaderViewportIndexLayerSupported(),
        .support_viewport_mask = device.IsNvViewportArray2Supported(),
        .support_geometry_shader_passthrough = device.IsNvGeometryShaderPassthroughSupported(),
        .support_conditional_barrier = device.SupportsConditionalBarriers(),
        .single_lane_geometry_subgroups = !profile.SupportsSubgroupStage(Shader::Stage::Geometry),
        .support_byte_addressed_r16 = true,
        .needs_lrz_disable = driver_id == VK_DRIVER_ID_QUALCOMM_PROPRIETARY,
        // The arrays are indexed per invocation, which needs nonuniform indexing to be defined.
        .support_handle_table_arrays = device.IsSampledImageArrayNonUniformIndexingSupported(),
    };
    host_info.ApplyDescriptorLimitPolicy();

    {
        // The descriptor arrays of bindless handle tables (HandleTableSize in texture_pass.cpp).
        const char* const table = std::getenv("LICHII_BINDLESS_TABLE");
        LOG_INFO(Render_Vulkan,
                 "Bindless handle tables: LICHII_BINDLESS_TABLE {}, sampled image arrays "
                 "indexed per invocation {}, up to {} sampled images per stage",
                 table != nullptr && table[0] != '\0' ? table : "default (64)",
                 device.IsSampledImageArrayNonUniformIndexingSupported() ? "supported"
                                                                         : "unsupported",
                 device.GetMaxPerStageDescriptorSampledImages());
    }

    if (device.GetMaxVertexInputAttributes() < Maxwell::NumVertexAttributes) {
        LOG_WARNING(Render_Vulkan, "maxVertexInputAttributes is too low: {} < {}",
                    device.GetMaxVertexInputAttributes(), Maxwell::NumVertexAttributes);
    }
    if (device.GetMaxVertexInputBindings() < Maxwell::NumVertexArrays) {
        LOG_WARNING(Render_Vulkan, "maxVertexInputBindings is too low: {} < {}",
                    device.GetMaxVertexInputBindings(), Maxwell::NumVertexArrays);
    }

    LOG_INFO(Render_Vulkan, "DynamicState setting value: {}", u32(Settings::values.dyna_state.GetValue()));
    kept_pipelines.store(KeptPipelines(0), std::memory_order::relaxed);
    LogEvictionPolicy();

    dynamic_features = {};
    // Keys without the driver, so that a driver update keeps the recorded pipelines
    dynamic_features.driver_id = 0;
    dynamic_features.driver_version = 0;

    // User granularity enforced in vulkan_device.cpp switch statement:
    //   Level 0: Core Dynamic States only
    //   Level 1: Core + EDS1
    //   Level 2: Core + EDS1 + EDS2 (accumulative)
    //   Level 3: Core + EDS1 + EDS2 + EDS3 (accumulative)
    // Here we only verify if extensions were successfully loaded by the device

    dynamic_features.has_extended_dynamic_state =
        device.IsExtExtendedDynamicStateSupported();

    dynamic_features.has_extended_dynamic_state_2 =
        device.IsExtExtendedDynamicState2Supported();
    dynamic_features.has_extended_dynamic_state_2_logic_op =
        device.IsExtExtendedDynamicState2ExtrasSupported();
    dynamic_features.has_extended_dynamic_state_2_patch_control_points = false;

    dynamic_features.has_extended_dynamic_state_3_blend =
        device.IsExtExtendedDynamicState3BlendingSupported();
    dynamic_features.has_extended_dynamic_state_3_enables =
        device.IsExtExtendedDynamicState3EnablesSupported();
    dynamic_features.has_color_write_enable =
        device.IsExtColorWriteEnableSupported();
    dynamic_features.has_dynamic_state3_depth_clamp_enable =
        dynamic_features.has_extended_dynamic_state_3_enables &&
        device.SupportsDynamicState3DepthClampEnable();
    dynamic_features.has_dynamic_state3_logic_op_enable =
        dynamic_features.has_extended_dynamic_state_3_enables &&
        device.SupportsDynamicState3LogicOpEnable();
    dynamic_features.has_dynamic_state3_line_stipple_enable =
        dynamic_features.has_extended_dynamic_state_3_enables &&
        device.SupportsDynamicState3LineStippleEnable();

    // VIDS: Independent toggle (not affected by dyna_state levels)
    dynamic_features.has_dynamic_vertex_input =
        device.IsExtVertexInputDynamicStateSupported() &&
        Settings::values.vertex_input_dynamic_state.GetValue();
    dynamic_features.has_static_vertex_repack =
        StaticVertexRepackEnabled(device.GetMinVertexInputBindingStrideAlignment());

    dynamic_features.has_provoking_vertex = device.IsExtProvokingVertexSupported();
    dynamic_features.has_provoking_vertex_first_mode =
        device.SupportsProvokingVertexFirstMode();
    dynamic_features.has_provoking_vertex_last_mode =
        device.SupportsProvokingVertexLastMode();
    dynamic_features.has_provoking_vertex_tf_preserve =
        device.SupportsTransformFeedbackProvokingVertexPreservation();

    const size_t cpu_threads = GetTotalPipelineWorkers();
    const size_t driver_threads =
        device.HasBrokenParallelShaderCompiling()
            ? 1
            : std::min(cpu_threads, std::max<size_t>(std::thread::hardware_concurrency() / 2, 2));
    shader_jobs.emplace(VideoCommon::ShaderJobConfig{
        .cpu_threads = cpu_threads,
        .driver_threads = driver_threads,
        .name = "VkShader",
    });
}

namespace {
// Pipelines the last two sessions used are built at boot. Each holds ~0.45 MB of driver memory:
// title 1 built 4,800 from eight sessions, 1.5 GB more than building them on first use (but that
// stalled frames 4x as often), while one session of its title screen uses ~1,500.
// MoltenVK keeps the previous eight sessions: a pipeline built while playing goes through
// SPIR-V -> MSL -> Metal, far more than the 2-3 ms the shorter window assumes, so each one
// built at boot is a stutter avoided.
// Ocarina still built pipelines for 1-2 s of every 10 s early in play with two sessions kept:
// keep sixteen (#161).
constexpr u8 UsedPipelinesMaxAge = 16;
constexpr u8 UsedPipelinesMaxAgeMoltenVK = 8;

u8 MaxUsedPipelineAge(const Device& device) {
    if (device.IsMoltenVK()) {
        return UsedPipelinesMaxAgeMoltenVK;
    }
    return UsedPipelinesMaxAge;
}

// Without a list of the pipelines recent sessions used (the first boot with this cache, or a session
// that ended before saving it), this many cached graphics pipelines at most are built at boot and the
// rest when first needed. A cache that grew over many builds held 48k pipelines, ~21 GB of driver
// memory built before the game started; the list is only saved at exit or after two minutes, so a
// boot that ran out of memory never saved one and the next boot did the same.
constexpr size_t MaxPipelinesWithoutUsedList = 3000;

// Captured environments the shader precache holds for skipped graphics pipelines; the rest wait
// for a later precache.
constexpr size_t PrecacheCaptureLimit = size_t{1} << 30;

// Shader cache v2: the records of the background prebuild the queue may hold, by payload bytes
constexpr u64 PrebuildCaptureLimit = u64{256} << 20;

// Shader cache v2: bytes a translation is taken to hold for the scheduler's budget. A
// translation's IR pools grow with the program; first guess until measured.
constexpr u64 PrecompiledTranslationBytes = u64{32} << 20;
u64 TranslationBytes(u64 record_bytes) {
    return std::clamp<u64>(record_bytes * 512, u64{8} << 20, u64{512} << 20);
}

u64 IrBytes(std::span<const Shader::IR::Program> programs) {
    u64 bytes{};
    for (const Shader::IR::Program& program : programs) {
        for (const Shader::IR::Block* const block : program.blocks) {
            bytes += sizeof(Shader::IR::Block) + block->Instructions().size() * sizeof(Shader::IR::Inst);
        }
    }
    return bytes;
}

// EDEN_NX2_PRELOAD_ALL_PIPELINES=1 builds every cached pipeline at boot, as before.
bool PreloadAllPipelines(bool moltenvk) {
    static const std::optional<bool> all = []() -> std::optional<bool> {
        const char* value = std::getenv("EDEN_NX2_PRELOAD_ALL_PIPELINES");
        return value != nullptr ? std::optional{value[0] == '1'} : std::nullopt;
    }();
    return all.value_or(moltenvk);
}

// Keep an area's pipelines across longer revisits (~7.5 min at 60 FPS, PR #1107).
constexpr u64 EvictAfterFrames = 27000;

/// Built graphics pipelines kept before unused ones are destroyed, for a session that loaded
/// `cached_at_boot` pipelines from the disk cache; 0 is the old eviction (Android, or
/// EDEN_NX2_KEPT_PIPELINES=0): every pipeline unused for EvictAfterFrames is destroyed except up to
/// MaxUnusedPrebuiltPipelines prebuilt ones no draw has used. EDEN_NX2_KEPT_PIPELINES=<n> sets the
/// limit on desktop; otherwise it fits the disk cache (KeptPipelinesForCache), up to a cap that
/// grows with the machine's RAM (MaxKeptPipelinesForMemory).
size_t KeptPipelines(size_t cached_at_boot) {
#ifdef __ANDROID__
    return 0;
#else
    static const std::optional<size_t> forced = []() -> std::optional<size_t> {
        const char* value = std::getenv("EDEN_NX2_KEPT_PIPELINES");
        if (value == nullptr || value[0] == '\0') {
            return std::nullopt;
        }
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(value, &end, 10);
        if (end == value || *end != '\0' || value[0] == '-') {
            LOG_WARNING(Render_Vulkan,
                        "EDEN_NX2_KEPT_PIPELINES={} is not a number, using the default", value);
            return std::nullopt;
        }
        return static_cast<size_t>(parsed);
    }();
    if (forced) {
        return *forced;
    }
    static const size_t max_kept =
        MaxKeptPipelinesForMemory(Common::GetMemInfo().TotalPhysicalMemory);
    return KeptPipelinesForCache(cached_at_boot, max_kept);
#endif
}
} // Anonymous namespace

/// Stops the background builds and workers, then saves the used pipeline list and the driver's
/// pipeline cache.
PipelineCache::~PipelineCache() {
    serialization_stop.request_stop();
    StopBackgroundJobs();
    // The pipelines' builds end before the driver pipeline cache they build into goes.
    shader_jobs->Shutdown();
    serialization_thread.WaitForRequests();
    SaveUsedPipelines();
    if (use_vulkan_pipeline_cache && !vulkan_pipeline_cache_filename.empty()) {
        SerializeVulkanPipelineCache(vulkan_pipeline_cache_filename, vulkan_pipeline_cache,
                                     CACHE_VERSION);
    }
    // After the records the serialization thread queued: its writer drains on destruction
    shader_cache_v2 = nullptr;
    cache_v2.reset();
    graphics_cache.clear();
    evicted_pipelines.clear();
    if (pipeline_binaries) {
        pipeline_binaries->LogCounts("at the end of the session");
        pipeline_binaries.reset();
    }
}

void PipelineCache::SaveUsedPipelines() {
    used_pipelines_saved = std::chrono::steady_clock::now();
    // A session too short to say what the game uses (a boot to the menu) keeps the old list.
    constexpr size_t MinimumUsed = 64;
    if (!cache_v2 || used_this_session.size() + used_and_evicted.size() < MinimumUsed) {
        return;
    }
    // This session's record, written by the cache's thread; older sessions keep theirs.
    VideoCommon::ShaderCacheV2::UsedRecord record{.session_time = used_session_time};
    for (const auto& [key, pipeline] : graphics_cache) {
        if (used_this_session.contains(pipeline.get())) {
            record.pipelines.push_back(key.Hash());
        }
    }
    record.pipelines.insert(record.pipelines.end(), used_and_evicted.begin(),
                            used_and_evicted.end());
    std::ranges::sort(record.pipelines);
    record.pipelines.erase(std::ranges::unique(record.pipelines).begin(), record.pipelines.end());
    cache_v2->Writer().Append(VideoCommon::ShaderCacheV2::RecordKind::Used, used_session_key,
                              VideoCommon::ShaderCacheV2::EncodeUsed(record));
}

GraphicsPipeline* PipelineCache::CurrentGraphicsPipeline() {

    if (!RefreshStages(graphics_key.unique_hashes)) {
        current_pipeline = nullptr;
        return nullptr;
    }
    if (maxwell3d->GetNativeMacroEngine().native_turing) {
        native_state.Refresh(*maxwell3d, dynamic_features);
        graphics_key.state = native_state;
        const VideoCommon::ShaderInfo* const vertex{shader_infos[1]};
        u32 mask = ~0U;
        if (maxwell3d->regs.IsMeshEnabled()) {
            mask = 0;
        } else if (vertex && !IsUnmaskedVertexProgram(vertex->unique_hash)) {
            mask = vertex->generic_read_mask;
        }
        graphics_key.state.CanonicalizeNative(mask);
    } else {
        graphics_key.state.Refresh(*maxwell3d, dynamic_features);
    }

    if (current_pipeline) {
        GraphicsPipeline* const next{current_pipeline->Next(graphics_key)};
        if (next) {
            current_pipeline = next;
            if (current_pipeline->IsPrecompiled() && current_pipeline->Failed()) {
                return ReplaceFailedPrecompiled();
            }
            current_pipeline->MarkUsed(frame_count);
            return BuiltPipeline(current_pipeline);
        }
    }
    return CurrentGraphicsPipelineSlowPath();
}

ComputePipeline* ComputePipelineCacheEntry::Get(bool native, GPUVAddr address,
                                                const ComputePipelineCacheKey& key) const {
    if (!pipeline && native) {
        throw Shader::RuntimeError(
            "Native compute pipeline unavailable: address={:#x} shader={:#016x} pipeline={:#016x}: {}",
            address, key.unique_hash, key.Hash(),
            failure.empty() ? "No compiler diagnostic was recorded" : failure);
    }
    return pipeline.get();
}

constexpr u32 MaxComputeRebuilds = 4;

namespace {
/// Which pointer slots a native program's arguments fill depends on the area (Ocarina of Time's
/// foliage culling fills different list slots in Hyrule Field and Castle Town), so a translation
/// rebuilt in one area may lack pointers the first cached one binds. Try first the translations
/// that bind every pointer the first one binds, those binding the most first, then the others in
/// cache order.
void OrderComputeTranslations(std::vector<CachedComputeTranslation>& candidates) {
    if (candidates.size() < 2) {
        return;
    }
    std::vector<std::vector<u64>> keys;
    keys.reserve(candidates.size());
    for (const CachedComputeTranslation& candidate : candidates) {
        keys.push_back(candidate.env.MappedStoragePointerKeys());
    }
    std::vector<size_t> order(candidates.size());
    std::iota(order.begin(), order.end(), size_t{0});
    const auto keeps_first = [&](size_t index) {
        return std::ranges::includes(keys[index], keys.front());
    };
    std::ranges::stable_sort(order, [&](size_t lhs, size_t rhs) {
        const bool lhs_keeps{keeps_first(lhs)};
        if (lhs_keeps != keeps_first(rhs)) {
            return lhs_keeps;
        }
        return lhs_keeps && keys[lhs].size() > keys[rhs].size();
    });
    std::vector<CachedComputeTranslation> ordered;
    ordered.reserve(candidates.size());
    for (const size_t index : order) {
        ordered.push_back(std::move(candidates[index]));
    }
    candidates = std::move(ordered);
}
} // Anonymous namespace

bool PipelineCache::StaleNullPointerSlot(const ComputePipelineCacheEntry& entry) const {
    const auto& qmd{kepler_compute->launch_description};
    for (const auto& [index, offset] : entry.null_pointer_slots) {
        if (index >= qmd.const_buffer_config.size() ||
            ((qmd.const_buffer_enable_mask >> index) & 1) == 0) {
            continue;
        }
        const auto& cbuf{qmd.const_buffer_config[index]};
        if (offset + sizeof(u64) > cbuf.size) {
            continue; // The translator reads past the bound size as zero, too
        }
        const GPUVAddr slot{cbuf.Address() + offset};
        if (!gpu_memory->IsFullyMappedRange(slot, sizeof(u64))) {
            continue;
        }
        const GPUVAddr address{gpu_memory->Read<u64>(slot)};
        if (address != 0 && gpu_memory->IsWithinGPUAddressRange(address) &&
            gpu_memory->IsFullyMappedRange(address, 1)) {
            LOG_INFO(Render_Vulkan,
                     "Rebuilding compute program {:#x}: c{}[{:#x}] was null at translation and "
                     "now points to {:#x}",
                     kepler_compute->regs.code_loc.Address() + qmd.program_start, index, offset, address);
            return true;
        }
    }
    return false;
}

ComputePipeline* PipelineCache::CurrentComputePipeline(u64& native_key) {

    const ShaderInfo* const shader{ComputeShader()};
    if (!shader) {
        return nullptr;
    }
    native_key = shader->lowered_native ? shader->lowered_native->native_program_key : 0;
    const auto& qmd{kepler_compute->launch_description};
    const ComputePipelineCacheKey key{
        .unique_hash = shader->unique_hash,
        .shared_memory_size = qmd.shared_alloc,
        .workgroup_size{qmd.block_dim_x, qmd.block_dim_y, qmd.block_dim_z},
    };
    const auto [pair, is_new]{compute_cache.try_emplace(key)};
    auto& entry{pair->second};
    if (is_new) {
        entry = CreateComputePipeline(key, shader);
    } else if (entry.pending) {
        if (entry.pending->pipeline->IsBuilt()) {
            const auto rebuilt{std::move(entry.pending)};
            if (rebuilt->pipeline->IsBound()) {
                LOG_INFO(Render_Vulkan, "Compute shader {:016x}: rebuilt pipeline in use",
                         key.unique_hash);
                retired_compute_pipelines.emplace_back(scheduler.CurrentTick(),
                                                       std::move(entry.pipeline));
                entry = std::move(*rebuilt);
            } else {
                LOG_WARNING(Render_Vulkan, "Keeping compute shader {:016x}: rebuild failed",
                            key.unique_hash);
                retired_compute_pipelines.emplace_back(scheduler.CurrentTick(),
                                                       std::move(rebuilt->pipeline));
                entry.rebuilds = MaxComputeRebuilds;
            }
        }
    } else if (entry.pipeline && entry.pipeline->IsPrecompiled() &&
               entry.pipeline->BuildFailed()) {
        precompiled_rejected.insert(key.Hash());
        precompiled_failed.fetch_add(1, std::memory_order_relaxed);
        retired_compute_pipelines.emplace_back(scheduler.CurrentTick(), std::move(entry.pipeline));
        entry = CreateComputePipeline(key, shader);
    } else if (!entry.null_pointer_slots.empty() && entry.rebuilds < MaxComputeRebuilds &&
               StaleNullPointerSlot(entry)) {
        // The translation dropped accesses through a pointer that was null back then. Which slots
        // the game fills depends on the area, so the new one keeps binding every pointer this one
        // binds, null now or not: a null pointer binds nothing.
        auto rebuilt{CreateComputePipeline(key, shader, entry.mapped_pointers)};
        if (rebuilt.pipeline) {
            rebuilt.rebuilds = entry.rebuilds + 1;
            rebuilt.used = entry.used;
            if (entry.pipeline) {
                entry.pending = std::make_unique<ComputePipelineCacheEntry>(std::move(rebuilt));
            } else {
                entry = std::move(rebuilt);
            }
        } else {
            // Keep the working translation when the new one cannot be built (for example a
            // narrow access that now stays global across two buffers), and stop retrying.
            LOG_WARNING(Render_Vulkan, "Keeping compute shader {:016x}: rebuild failed: {}",
                        key.unique_hash, rebuilt.failure);
            entry.rebuilds = MaxComputeRebuilds;
        }
    }
    if (VideoCommon::StutterTrace::Enabled() && !std::exchange(entry.used, true)) {
        VideoCommon::StutterTrace::CountLookup(
            is_new ? VideoCommon::StutterTrace::Lookup::ComputeNew
                   : VideoCommon::StutterTrace::Lookup::ComputePrebuilt);
    }
    return entry.Get(kepler_compute->IsNativeMode(),
                     kepler_compute->regs.code_loc.Address() + qmd.program_start, key);
}

/// Loads the title's pipeline cache from disk and builds its pipelines, reporting progress through
/// `callback`, runs the shader precache when this boot asked for it, then (unless loading was
/// stopped) starts the background builds.
void PipelineCache::LoadDiskResources(u64 title_id, std::stop_token stop_loading,
                                      const VideoCore::DiskResourceLoadCallback& callback) {
    if (title_id == 0) {
        return;
    }
    if (!pipeline_cache_filename.empty()) {
        StopBackgroundJobs();
        serialization_thread.WaitForRequests();
        if (use_vulkan_pipeline_cache && !vulkan_pipeline_cache_filename.empty()) {
            SerializeVulkanPipelineCache(vulkan_pipeline_cache_filename, vulkan_pipeline_cache,
                                         CACHE_VERSION);
        }
        shader_cache_v2 = nullptr;
        cache_v2.reset();
        pipeline_binaries.reset();
    }

    namespace StutterTrace = VideoCommon::StutterTrace;
    const auto load_begin = std::chrono::steady_clock::now();
    StutterTrace::BootMarker("shader_load_begin");
    const auto shader_dir{Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir)};
    const auto base_dir{shader_dir / fmt::format("{:016x}", title_id)};
    if (!Common::FS::CreateDir(shader_dir) || !Common::FS::CreateDir(base_dir)) {
        LOG_ERROR(Common_Filesystem, "Failed to create pipeline cache directories");
        return;
    }
    namespace CacheV2 = VideoCommon::ShaderCacheV2;
    pipeline_cache_filename = base_dir / CacheV2::FileName;
    const auto migration =
        MigrateLegacyPipelineCache(base_dir, pipeline_cache_filename, MaxUsedPipelineAge(device));
    if (migration.version) {
        LOG_INFO(Render_Vulkan,
                 "Shader cache v2 started from the version {} cache: {} graphics and {} "
                 "compute pipelines ({} records collapsed), {} used-list entries",
                 *migration.version, migration.graphics, migration.compute, migration.collapsed,
                 migration.used);
    } else if (migration.failed) {
        LOG_WARNING(Render_Vulkan, "Couldn't start shader cache v2 from the older cache");
    }
    cache_v2 = std::make_unique<CacheV2::Cache>(pipeline_cache_filename);
    shader_cache_v2 = cache_v2.get();
    pipeline_binaries = PipelineBinaries::Open(device, base_dir);
    // Without the lock: another emulator's appends are complete records or a cut-off tail.
    // The payload hashes are checked on the shader job workers.
    auto log_v2 = std::make_shared<CacheV2::LogFile>(CacheV2::ReadLogFile(
        pipeline_cache_filename, [this](size_t count, const std::function<void(size_t)>& task) {
            VideoCommon::ParallelFor(*shader_jobs, count, task);
        }));
    cache_v2->LoadPrograms(log_v2->parsed.live);
    const CacheV2::LogStats& stats = log_v2->parsed.stats;
    LOG_INFO(Render_Vulkan,
             "Shader cache v2: {} bytes, {} live records, {} superseded, {} tombstones, {} "
             "damaged headers, {} damaged records, {} bytes skipped, {} bytes cut off",
             stats.file_size, stats.live, stats.superseded, stats.tombstones, stats.bad_headers,
             stats.bad_payloads, stats.skipped_bytes, stats.torn_tail_bytes);
    std::vector<u64> expired_sessions;
    used_pipelines =
        UsedPipelinesFromLog(log_v2->parsed.live, MaxUsedPipelineAge(device), expired_sessions);
    for (const u64 session : expired_sessions) {
        cache_v2->Writer().Append(CacheV2::RecordKind::Tombstone, session,
                                  CacheV2::TombstonePayload(CacheV2::RecordKind::Used));
    }
    used_session_time = static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count());
    used_session_key = used_session_time ^ std::random_device{}();
    used_pipelines_saved = std::chrono::steady_clock::now();

    // With the shader precache this boot, the cached graphics pipelines skipped below are built
    // on the loading screen too, unless an earlier precache built them for this driver.
    namespace ShaderWarm = VideoCommon::ShaderWarm;
    const bool precache = ShaderWarm::PrecacheBeforeStart(
        Settings::values.precache_shaders.GetValue(), ShaderWarm::TakeWarmBeforeStart());
    std::unordered_set<u64> precached_pipelines;
    if (precache) {
        precached_pipelines = ShaderWarm::ReadPrecachedPipelines(
            ShaderWarm::PrecachedPipelinesPath(title_id),
            {device.GetDriverVersion(), device.GetDeviceID(), CACHE_VERSION});
    }
    if (use_vulkan_pipeline_cache) {
        vulkan_pipeline_cache_filename =
            VideoCommon::VersionedPipelineCachePath(base_dir, "vulkan_pipelines", CACHE_VERSION);
        if (VideoCommon::CarryOverPipelineCache(
                base_dir, "vulkan_pipelines", VULKAN_CACHE_MAGIC_NUMBER,
                VideoCommon::CompatibleVulkanPipelineCacheVersions, CACHE_VERSION)
                .failed) {
            LOG_WARNING(Render_Vulkan,
                        "Couldn't copy the older driver pipeline cache; this one starts empty");
        }
        vulkan_pipeline_cache =
            LoadVulkanPipelineCache(vulkan_pipeline_cache_filename, CACHE_VERSION);
    }
    // A kill or driver crash in a build of the boot load, precache or prebuild leaves its id in
    // this file; the id is skipped from the next start on, until the driver changes.
    // GetTotalPipelineWorkers() covers the driver workers; the rest are threads that run a
    // promoted job's driver stage.
    build_journal.emplace(base_dir / "driver-builds",
                          DriverBuildStamp(device.GetDriverID(), device.GetDriverVersion(),
                                           device.GetVendorID(), device.GetDeviceID(),
                                           CACHE_VERSION, CacheV2::FormatVersion),
                          GetTotalPipelineWorkers() + 4, "pipeline");
    LoadPipelinesV2(title_id, std::move(log_v2), stop_loading, callback, load_begin, precache,
                    std::move(precached_pipelines));
}

bool PipelineCache::KeyFitsDevice(const GraphicsPipelineCacheKey& key) const {
    if ((key.state.extended_dynamic_state != 0) != dynamic_features.has_extended_dynamic_state ||
        (key.state.extended_dynamic_state_2 != 0) !=
            dynamic_features.has_extended_dynamic_state_2 ||
        (key.state.extended_dynamic_state_2_logic_op != 0) !=
            dynamic_features.has_extended_dynamic_state_2_logic_op ||
        (key.state.extended_dynamic_state_3_blend != 0) !=
            dynamic_features.has_extended_dynamic_state_3_blend ||
        (key.state.extended_dynamic_state_3_enables != 0) !=
            dynamic_features.has_extended_dynamic_state_3_enables ||
        (key.state.color_write_enable_dynamic != 0) != dynamic_features.has_color_write_enable ||
        (key.state.dynamic_vertex_input != 0) != dynamic_features.has_dynamic_vertex_input) {
        return false;
    }
    const bool key_requests_provoking_last = key.state.provoking_vertex_last != 0;
    if (key_requests_provoking_last && !dynamic_features.has_provoking_vertex_last_mode) {
        // The live draw retains this bit too; pipeline construction falls back to first vertex.
        return true;
    }
    const bool key_uses_transform_feedback = key.state.xfb_enabled != 0;
    return !key_uses_transform_feedback || !key_requests_provoking_last ||
           dynamic_features.has_provoking_vertex_tf_preserve;
}

PipelineBuildQueue PipelineCache::BuildQueue(bool build_in_parallel, u64 cpu_bytes,
                                             bool native_mesh) {
    if (!build_in_parallel) {
        return {.owner_builds = true};
    }
    return {.jobs = &*shader_jobs,
            .cpu_bytes = cpu_bytes,
            .driver_bytes = GraphicsDriverBytes(native_mesh)};
}

std::shared_ptr<ShaderPools> PipelineCache::TakeSparePools() {
    {
        std::scoped_lock lock{spare_pools_mutex};
        if (!spare_pools.empty()) {
            auto pools = std::move(spare_pools.back());
            spare_pools.pop_back();
            return pools;
        }
    }
    return std::make_shared<ShaderPools>();
}

void PipelineCache::ReturnPools(std::shared_ptr<ShaderPools> pools) {
    pools->ReleaseContents();
    std::scoped_lock lock{spare_pools_mutex};
    if (spare_pools.size() < 4) {
        spare_pools.push_back(std::move(pools));
    }
}

void PipelineCache::StopBackgroundJobs() {
    scheduled_prebuild.reset();
}

std::unique_ptr<GraphicsPipeline> PipelineCache::TranslateCachedGraphics(
    std::span<const u8> payload, const GraphicsPipelineCacheKey& key,
    PipelineStatistics* statistics, bool& incomplete) {
    std::vector<FileEnvironment> envs;
    Common::UniqueFunction<void, std::istream&, FileEnvironment> read_compute{
        [](std::istream&, FileEnvironment) {
            throw std::ios_base::failure("Compute pipeline in another kind of record");
        }};
    Common::UniqueFunction<void, std::istream&, std::vector<FileEnvironment>> read_graphics{
        [&](std::istream& file, std::vector<FileEnvironment> read) {
            GraphicsPipelineCacheKey read_key;
            file.read(reinterpret_cast<char*>(&read_key), sizeof(read_key));
            VideoCommon::CheckGraphicsRecord(read_key.unique_hashes, read.size());
            envs = std::move(read);
        }};
    VideoCommon::ReadPipelinePayload(payload, read_compute, read_graphics);
    ShaderPools pools;
    boost::container::static_vector<Shader::Environment*, Maxwell::MaxShaderProgram> env_ptrs;
    for (auto& env : envs) {
        env_ptrs.push_back(&env);
    }
    auto pipeline{CreateGraphicsPipeline(pools, key, MakeSpan(env_ptrs), statistics, false)};
    incomplete = !pipeline && std::ranges::any_of(envs, [this](const FileEnvironment& env) {
        return env.LacksRecordedValue(host_info.EnvironmentReadProperties());
    });
    return pipeline;
}

void PipelineCache::LoadPipelinesV2(u64 title_id,
                                    std::shared_ptr<const VideoCommon::ShaderCacheV2::LogFile> log,
                                    std::stop_token stop_loading,
                                    const VideoCore::DiskResourceLoadCallback& callback,
                                    std::chrono::steady_clock::time_point load_begin,
                                    bool precache, std::unordered_set<u64> precached_pipelines) {
    namespace CacheV2 = VideoCommon::ShaderCacheV2;
    namespace StutterTrace = VideoCommon::StutterTrace;
    using CacheV2::RecordKind;
    using VideoCommon::ShaderJobPriority;
    auto& jobs = *shader_jobs;
    const u32 read_properties = host_info.EnvironmentReadProperties();

    // The boot builds (Soon), which Loading Shaders waits for, and the background prebuild. A
    // stopped load cancels both; the workers stay for the game.
    const VideoCommon::ShaderJobBatch boot = jobs.CreateBatch();
    scheduled_prebuild.emplace(jobs, PrebuildCaptureLimit);
    const std::stop_callback cancel_load{
        stop_loading, [&jobs, boot, prebuild = scheduled_prebuild->Batch()] {
            jobs.Cancel(boot);
            jobs.Cancel(prebuild);
        }};

    struct {
        std::mutex mutex;
        size_t total{};
        size_t built{};
        bool has_loaded{};
        std::unique_ptr<PipelineStatistics> statistics;
        /// Records the load never chooses, as in the old cache; begin is the index in records
        std::vector<VideoCommon::PipelineRecord> incomplete;
        std::vector<std::pair<u64, VideoCommon::PipelineRecord>> incomplete_graphics;
        std::vector<VideoCommon::PipelineRecord> copies;
    } state;
    if (device.IsKhrPipelineExecutablePropertiesEnabled()) {
        state.statistics = std::make_unique<PipelineStatistics>(device);
    }
    // Under state.mutex
    const auto built_one = [&state, &callback] {
        ++state.built;
        if (state.has_loaded) {
            callback(VideoCore::LoadCallbackStage::Build, state.built, state.total);
        }
    };
    // The graphics and compute records read, by the index a PipelineRecord's begin stands for
    std::vector<const CacheV2::LogRecord*> records;
    const auto pipeline_record = [&records](size_t index) {
        return VideoCommon::PipelineRecord{.begin = index, .end = index + 1, .hash = records[index]->key};
    };
    // Intact, but not a record this build reads: it goes.
    const auto unreadable = [this](const CacheV2::LogRecord& record, const char* reason) {
        LOG_ERROR(Common_Filesystem, "Shader cache v2 record {:016x}: {}", record.key, reason);
        cache_v2->Writer().Append(RecordKind::Tombstone, record.key,
                                  CacheV2::TombstonePayload(record.kind));
    };

    // One pass over the records without reading them, which the jobs do in parallel: all
    // translations of a compute key go to one job, graphics records by their key's hash.
    ::Common::unordered_map<ComputePipelineCacheKey, std::vector<size_t>> compute_keys;
    std::vector<std::pair<size_t, GraphicsPipelineCacheKey>> boot_graphics;
    struct PrebuildRecord {
        GraphicsPipelineCacheKey key;
        std::vector<u8> payload;
    };
    std::vector<PrebuildRecord> prebuild;
    u64 prebuild_bytes = 0;
    std::vector<PrecacheJob> precache_graphics;
    u64 precache_capture_bytes = 0;
    std::vector<CacheV2::ProgramRecord> stale_programs;
    for (const CacheV2::LogRecord& record : log->parsed.live) {
        if (record.kind != RecordKind::Program) {
            continue;
        }
        const auto stamp = CacheV2::ProgramStamp(record.payload);
        if (!stamp) {
            continue;
        }
        cache_v2->AddProgram(record.key, *stamp);
        if (*stamp != CacheV2::LoweringStamp()) {
            if (auto program = CacheV2::DecodeProgram(record.payload)) {
                stale_programs.push_back(std::move(*program));
            }
        }
    }
    // Programs another build recorded are lowered again before any of their pipelines is built:
    // the records of those whose code or texture bound buffer changed move to this build's code
    // (a pipeline key holds the code's hash alone, so one built from the old record would be hit
    // with the new texture bound buffer). Their own records follow last, so that a load cut short
    // does this again at the next boot.
    std::vector<CacheV2::LogAppend> relinked_appends;
    std::vector<CacheV2::LogRecord> relinked_records;
    std::unordered_set<u64> replaced_records;
    if (!stale_programs.empty() && !stop_loading.stop_requested()) {
        const size_t stale_count = stale_programs.size();
        CacheV2::ForEach for_each = [&jobs](size_t count, const std::function<void(size_t)>& task) {
            VideoCommon::ParallelFor(jobs, count, task);
        };
        RevalidatedPrograms revalidated =
            RevalidateProgramRecords(std::move(stale_programs), for_each);
        relinked_appends =
            RelinkPipelineRecords(log->parsed.live, revalidated.relinked,
                                  [&stop_loading] { return stop_loading.stop_requested(); });
        if (!stop_loading.stop_requested()) {
            for (const CacheV2::LogAppend& append : relinked_appends) {
                cache_v2->Writer().Append(append.kind, append.key, append.payload);
                replaced_records.insert(append.key);
                if (append.kind == RecordKind::Tombstone) {
                    continue;
                }
                relinked_records.push_back({.kind = append.kind,
                                            .flags = 0,
                                            .key = append.key,
                                            .offset = 0,
                                            .payload = append.payload});
            }
            for (const CacheV2::LogAppend& append :
                 RelinkUsedPipelines(log->parsed.live, relinked_appends, MaxUsedPipelineAge(device),
                                     used_pipelines)) {
                cache_v2->Writer().Append(append.kind, append.key, append.payload);
            }
            for (const CacheV2::ProgramRecord& program : revalidated.unchanged) {
                cache_v2->RecordProgram(program);
            }
            for (const auto& [hash, relinked] : revalidated.relinked) {
                cache_v2->RecordProgram(relinked.program);
            }
            LOG_INFO(Render_Vulkan,
                     "Shader cache v2: {} programs of another build lowered again, {} to other "
                     "code or texture bound buffer, {} pipeline records moved to it",
                     stale_count, revalidated.relinked.size(), relinked_records.size());
        }
    }
    std::vector<const CacheV2::LogRecord*> plan;
    plan.reserve(log->parsed.live.size() + relinked_records.size());
    for (const CacheV2::LogRecord& record : log->parsed.live) {
        if (record.kind == RecordKind::Program ||
            ((record.kind == RecordKind::Graphics || record.kind == RecordKind::Compute) &&
             replaced_records.contains(record.key))) {
            continue;
        }
        plan.push_back(&record);
    }
    for (const CacheV2::LogRecord& record : relinked_records) {
        plan.push_back(&record);
    }

    size_t graphics_records = 0;
    size_t compute_records = 0;
    size_t skipped_pipelines = 0;
    size_t queued_graphics = 0;
    // Native keys are canonical (FixedPipelineState::CanonicalizeNative). A record an older build
    // wrote holds its key as that draw made it: it is built under the canonical key, which draws
    // now look up, and the used list may name it by either. Reading the environments for this
    // runs on the shader job workers.
    std::vector<std::optional<GraphicsPipelineCacheKey>> canonical_keys(plan.size());
    VideoCommon::ParallelFor(jobs, plan.size(), [&](size_t index) {
        const CacheV2::LogRecord& record = *plan[index];
        if (record.kind != RecordKind::Graphics || stop_loading.stop_requested()) {
            return;
        }
        Common::UniqueFunction<void, std::istream&, FileEnvironment> read_compute{
            [](std::istream&, FileEnvironment) {}};
        Common::UniqueFunction<void, std::istream&, std::vector<FileEnvironment>> read_graphics{
            [&](std::istream& file, std::vector<FileEnvironment> envs) {
                GraphicsPipelineCacheKey key;
                file.read(reinterpret_cast<char*>(&key), sizeof(key));
                if (!file || envs.size() > Maxwell::MaxShaderProgram) {
                    return;
                }
                boost::container::static_vector<const Shader::Environment*,
                                                Maxwell::MaxShaderProgram>
                    env_ptrs;
                for (const auto& env : envs) {
                    env_ptrs.push_back(&env);
                }
                CanonicalizeNativeKey(key, env_ptrs);
                canonical_keys[index] = key;
            }};
        try {
            VideoCommon::ReadPipelinePayload(record.payload, read_compute, read_graphics);
        } catch (const std::exception&) {
            // Unreadable: the plan below reports it when it reads the key, or its build does.
        }
    });
    for (size_t plan_index = 0; plan_index < plan.size(); ++plan_index) {
        const CacheV2::LogRecord& record = *plan[plan_index];
        if (stop_loading.stop_requested()) {
            break;
        }
        if (record.kind == RecordKind::Compute) {
            ++compute_records;
            const auto key = PipelineRecordKey<ComputePipelineCacheKey>(record.payload);
            if (!key) {
                unreadable(record, "Compute pipeline record without a key");
                continue;
            }
            compute_keys[*key].push_back(records.size());
            records.push_back(&record);
            continue;
        }
        if (record.kind != RecordKind::Graphics) {
            continue;
        }
        ++graphics_records;
        auto key = PipelineRecordKey<GraphicsPipelineCacheKey>(record.payload);
        if (!key) {
            unreadable(record, "Graphics pipeline record without a key");
            continue;
        }
        const u64 stored_hash = key->Hash();
        if (canonical_keys[plan_index]) {
            key = canonical_keys[plan_index];
        }
        const size_t index = records.size();
        records.push_back(&record);
        if (!KeyFitsDevice(*key)) {
            continue;
        }
        const u64 hash = key->Hash();
        if (!disk_pipelines.insert(hash).second) {
            continue;
        }
        if (build_journal->Skip(hash)) {
            continue;
        }
        if (PreloadAllPipelines(device.IsMoltenVK()) || (used_pipelines.empty()
                                          ? queued_graphics < MaxPipelinesWithoutUsedList
                                          : used_pipelines.contains(hash) || used_pipelines.contains(stored_hash))) {
            ++queued_graphics;
            boot_graphics.emplace_back(index, *key);
            continue;
        }
        ++skipped_pipelines;
        const u64 bytes = record.payload.size();
        std::vector<u8> payload(record.payload.begin(), record.payload.end());
        if (precache && !precached_pipelines.contains(hash) &&
            bytes <= PrecacheCaptureLimit - precache_capture_bytes) {
            precache_capture_bytes += bytes;
            precache_graphics.push_back({
                .graphics_hash = hash,
                .journal_id = hash,
                .translate =
                    [this, key = *key, payload = std::move(payload)](
                        VideoCommon::ShaderJobContext& context) -> Common::UniqueFunction<bool> {
                    bool incomplete{};
                    auto pipeline = TranslateCachedGraphics(payload, key, nullptr, incomplete);
                    if (!pipeline) {
                        return {};
                    }
                    context.SetDriverBytes(pipeline->DriverBuildBytes());
                    return [pipeline = std::move(pipeline)] {
                        pipeline->Build();
                        return !pipeline->Failed();
                    };
                },
                .cpu_bytes = TranslationBytes(bytes),
            });
        } else if (bytes <= PrebuildCaptureLimit - prebuild_bytes) {
            prebuild_bytes += bytes;
            prebuild.push_back({*key, std::move(payload)});
        }
    }

    for (auto& [key, indices] : compute_keys) {
        if (build_journal->Skip(key.Hash())) {
            continue;
        }
        ++state.total;
        u64 bytes = 0;
        for (const size_t index : indices) {
            bytes = std::max<u64>(bytes, records[index]->payload.size());
        }
        jobs.Submit(
            {
                .priority = ShaderJobPriority::Soon,
                .cpu_bytes = TranslationBytes(bytes),
                .driver_bytes = DriverCompileBytes,
                .cpu = [this, &state, &built_one, &records, &pipeline_record, &unreadable, key,
                        indices = std::move(indices),
                        read_properties](VideoCommon::ShaderJobContext& context)
                    -> VideoCommon::ShaderDriverStage {
                    std::vector<CachedComputeTranslation> candidates;
                    for (const size_t index : indices) {
                        const CacheV2::LogRecord& record = *records[index];
                        Common::UniqueFunction<void, std::istream&, FileEnvironment> read_compute{
                            [&](std::istream& file, FileEnvironment env) {
                                ComputePipelineCacheKey read_key;
                                file.read(reinterpret_cast<char*>(&read_key), sizeof(read_key));
                                VideoCommon::CheckComputeRecord(env, read_key.shared_memory_size,
                                                                read_key.workgroup_size);
                                candidates.push_back({std::move(env), pipeline_record(index)});
                            }};
                        Common::UniqueFunction<void, std::istream&, std::vector<FileEnvironment>>
                            read_graphics{[](std::istream&, std::vector<FileEnvironment>) {
                                throw std::ios_base::failure(
                                    "Graphics pipeline in another kind of record");
                            }};
                        try {
                            VideoCommon::ReadPipelinePayload(record.payload, read_compute,
                                                             read_graphics);
                        } catch (const std::ios_base::failure& e) {
                            unreadable(record, e.what());
                        }
                    }
                    OrderComputeTranslations(candidates);
                    ShaderPools pools;
                    ComputePipelineCacheEntry entry;
                    size_t chosen{};
                    for (; chosen < candidates.size() && !context.StopRequested(); ++chosen) {
                        pools.ReleaseContents();
                        entry = CreateComputePipeline(pools, key, candidates[chosen].env,
                                                      state.statistics.get(), false);
                        if (entry.pipeline) {
                            break;
                        }
                    }
                    std::scoped_lock lock{state.mutex};
                    AddUnchosenComputeTranslations(candidates, chosen, read_properties,
                                                   state.incomplete, state.copies);
                    if (!entry.pipeline) {
                        built_one();
                        return {};
                    }
                    return [this, &state, &built_one, key, entry = std::move(entry)]() mutable {
                        {
                            const DriverBuildJournal::Scope recorded{&*build_journal, key.Hash()};
                            entry.pipeline->Build();
                        }
                        std::scoped_lock driver_lock{state.mutex};
                        compute_cache.emplace(key, std::move(entry));
                        built_one();
                    };
                },
            },
            &boot);
    }
    compute_keys.clear();
    for (const auto& [index, key] : boot_graphics) {
        ++state.total;
        const CacheV2::LogRecord& record = *records[index];
        jobs.Submit(
            {
                .priority = ShaderJobPriority::Soon,
                .cpu_bytes = TranslationBytes(record.payload.size()),
                .driver_bytes = DriverCompileBytes,
                .cpu = [this, &state, &built_one, &pipeline_record, &unreadable, &record, index,
                        key](VideoCommon::ShaderJobContext& context)
                    -> VideoCommon::ShaderDriverStage {
                    std::unique_ptr<GraphicsPipeline> pipeline;
                    bool incomplete{};
                    if (!context.StopRequested()) {
                        try {
                            pipeline = TranslateCachedGraphics(record.payload, key,
                                                               state.statistics.get(), incomplete);
                        } catch (const std::ios_base::failure& e) {
                            unreadable(record, e.what());
                        }
                    }
                    if (!pipeline) {
                        std::scoped_lock lock{state.mutex};
                        if (incomplete) {
                            state.incomplete_graphics.emplace_back(key.Hash(),
                                                                   pipeline_record(index));
                        }
                        built_one();
                        return {};
                    }
                    context.SetDriverBytes(pipeline->DriverBuildBytes());
                    return [this, &state, &built_one, key,
                            pipeline = std::move(pipeline)]() mutable {
                        {
                            const DriverBuildJournal::Scope recorded{&*build_journal, key.Hash()};
                            pipeline->Build();
                        }
                        std::scoped_lock lock{state.mutex};
                        if (!pipeline->Failed()) {
                            graphics_cache.emplace(key, std::move(pipeline));
                        }
                        built_one();
                    };
                },
            },
            &boot);
    }
    boot_graphics.clear();

    // With the precache, the prebuild starts after it.
    size_t deferred_pipelines = 0;
    const auto start_prebuild = [&] {
        // Newest first: the log appends, so its end holds what the latest sessions found, which
        // the game is likelier to need soon than its oldest records.
        for (auto it = prebuild.rbegin(); it != prebuild.rend(); ++it) {
            PrebuildRecord& record = *it;
            const u64 bytes = record.payload.size();
            const bool added = scheduled_prebuild->Add(
                record.key, bytes, TranslationBytes(bytes), DriverCompileBytes,
                [this, key = record.key, payload = std::move(record.payload)] {
                    bool incomplete{};
                    try {
                        return TranslateCachedGraphics(payload, key, nullptr, incomplete);
                    } catch (const std::ios_base::failure&) {
                        return std::unique_ptr<GraphicsPipeline>{};
                    }
                },
                &*build_journal, record.key.Hash());
            deferred_pipelines += added ? 1 : 0;
        }
        prebuild.clear();
    };
    if (!precache) {
        start_prebuild();
    }

    LOG_INFO(Render_Vulkan, "Total Pipeline Count: {}", state.total);
    if (build_journal->Skipped() != 0) {
        LOG_INFO(Render_Vulkan,
                 "Shader cache v2: {} cached pipelines left out of the boot load, precache and "
                 "prebuild: a driver crash ended an earlier one on them (they build when drawn)",
                 build_journal->Skipped());
    }
    kept_pipelines.store(KeptPipelines(state.total), std::memory_order::relaxed);
    LogEvictionPolicy();
    if (skipped_pipelines != 0) {
        LOG_INFO(Render_Vulkan,
                 "{} cached graphics pipelines skipped at boot, {} {} for background prebuild",
                 skipped_pipelines, precache ? prebuild.size() : deferred_pipelines,
                 precache ? "held" : "admitted");
    }
    {
        std::scoped_lock lock{state.mutex};
        callback(VideoCore::LoadCallbackStage::Build, state.built, state.total);
        state.has_loaded = true;
    }
    boot.Wait();

    std::vector<DuplicateGraphicsRecord> no_duplicates;
    AddIncompleteGraphics(state.incomplete_graphics, no_duplicates, disk_pipelines,
                          state.incomplete);
    // The records the load never chooses go by tombstones, and the log is compacted once a
    // quarter of it is records no load reads.
    if (!stop_loading.stop_requested()) {
        for (const auto* removed : {&state.incomplete, &state.copies}) {
            for (const VideoCommon::PipelineRecord& record : *removed) {
                const CacheV2::LogRecord& read = *records[record.begin];
                cache_v2->Writer().Append(RecordKind::Tombstone, read.key,
                                          CacheV2::TombstonePayload(read.kind));
            }
        }
        const CacheV2::LogStats& stats = log->parsed.stats;
        if (stats.dead_bytes != 0 && stats.dead_bytes >= stats.file_size / 4) {
            cache_v2->Writer().Compact();
        }
    }
    records.clear();
    log.reset();

    PreparePrecompiled(pipeline_cache_filename.parent_path(), precache, stop_loading, callback);

    if (use_vulkan_pipeline_cache) {
        SerializeVulkanPipelineCache(vulkan_pipeline_cache_filename, vulkan_pipeline_cache,
                                     CACHE_VERSION);
        size_t size = 0;
        vulkan_pipeline_cache.Read(&size, nullptr);
        last_cache_size.store(size, std::memory_order_relaxed);
        last_flush = std::chrono::steady_clock::now();
    }
    if (state.statistics) {
        state.statistics->Report();
    }
    if (StutterTrace::Enabled()) {
        const auto bytes = [](const std::filesystem::path& path) {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            return error ? std::uintmax_t{0} : size;
        };
        StutterTrace::SetCacheFile(Common::FS::PathToUTF8String(pipeline_cache_filename));
        StutterTrace::BootMarker(
            "shader_load_end",
            fmt::format("seconds={:.3f} graphics_records={} compute_records={} queued={} skipped={} "
                        "deferred={} cache_bytes={} driver_cache_bytes={}",
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - load_begin)
                            .count(),
                        graphics_records, compute_records, state.total, skipped_pipelines,
                        precache ? prebuild.size() : deferred_pipelines,
                        bytes(pipeline_cache_filename), bytes(vulkan_pipeline_cache_filename)));
    }
    if (precache && !stop_loading.stop_requested()) {
        PrecacheShaders(title_id, std::move(precache_graphics), std::move(precached_pipelines),
                        stop_loading, callback);
    }
    if (stop_loading.stop_requested()) {
        scheduled_prebuild.reset();
    } else if (precache) {
        start_prebuild();
        LOG_INFO(Render_Vulkan, "{} cached graphics pipelines admitted for background prebuild",
                 deferred_pipelines);
    }
}

namespace {
/// Threads of the shader precache: EDEN_NX2_WARM_THREADS=N (1 to 64), else the
/// shader_warm_threads setting, where 0 is every CPU thread that fits the free commit memory.
size_t PrecacheThreads() {
    const size_t all = std::clamp<size_t>(std::thread::hardware_concurrency(), 1, 64);
    const u32 setting = Settings::values.shader_warm_threads.GetValue();
    const size_t chosen = setting == 0 ? all : std::clamp<size_t>(setting, 1, 64);
    static const size_t forced = [] {
        constexpr size_t Default = 0;
        const char* const value = std::getenv("EDEN_NX2_WARM_THREADS");
        if (value == nullptr || *value == '\0') {
            return Default;
        }
        char* end = nullptr;
        const long parsed = std::strtol(value, &end, 10);
        if (end == value || *end != '\0' || parsed < 1 || parsed > 64) {
            LOG_WARNING(Render_Vulkan,
                        "Nx2Perf: EDEN_NX2_WARM_THREADS={} is not a number from 1 to 64, using "
                        "the setting",
                        value);
            return Default;
        }
        return static_cast<size_t>(parsed);
    }();
    if (forced != 0 || setting != 0) {
        return forced != 0 ? forced : chosen;
    }
    // Every CPU thread, unless the system is short of commit memory: each warm-up thread holds a
    // driver compile, and on a 16 GB machine all 12 threads plus the guest memory hit the commit
    // limit (#741). Windows reports the commit charge; elsewhere the count stays as is.
    constexpr u64 BytesPerThread = 2ULL << 30;
    constexpr u64 Headroom = 2ULL << 30;
    const auto usage = Common::GetProcessMemoryUsage();
    if (usage.system_commit_limit == 0 || usage.system_commit >= usage.system_commit_limit) {
        return chosen;
    }
    const u64 free_commit = usage.system_commit_limit - usage.system_commit;
    const size_t fit = static_cast<size_t>(
        std::max<u64>(free_commit > Headroom ? (free_commit - Headroom) / BytesPerThread : 0, 1));
    if (fit < chosen) {
        LOG_INFO(Render_Vulkan,
                 "Shader precache: {} MiB of commit memory free, {} threads instead of {} "
                 "(shader_warm_threads or EDEN_NX2_WARM_THREADS set a fixed count)",
                 free_commit >> 20, fit, chosen);
        return fit;
    }
    return chosen;
}

bool ComputeNoInline();

/// Whether shared shader functions are built as calls (compute bit 0, graphics bit 1), for the
/// precache markers: the SPIR-V the driver compiled differs between the two modes.
u32 NoInlineBits() {
    return (ComputeNoInline() ? 1U : 0U) | (GraphicsNoInline() ? 2U : 0U);
}

/// The graphics link mode, for the precache markers (bit 0 fast links usable, bit 1 optimized
/// links on): together they decide whether libraries keep link time optimization info, which
/// changes what the driver caches.
u32 LinkModeBits(const Device& device) {
    return (device.CanFastLinkGraphicsLibraries() ? 1U : 0U) | (GplOptimizeEnabled() ? 2U : 0U);
}
} // Anonymous namespace

size_t PipelineCache::PrecacheGraphicsWarm(u64 title_id, size_t num_threads, size_t done_before,
                                           size_t total_before, std::stop_token stop_loading,
                                           const VideoCore::DiskResourceLoadCallback& callback,
                                           bool& skipped) {
    namespace ShaderWarm = VideoCommon::ShaderWarm;
    if (!device.IsExtGraphicsPipelineLibrarySupported()) {
        return 0;
    }
    const std::filesystem::path warm_file = ShaderWarm::GraphicsWarmPath(title_id);
    const std::string path = Common::FS::PathToUTF8String(warm_file); // for the log
    std::error_code ec;
    if (!std::filesystem::exists(warm_file, ec)) {
        return 0;
    }
    const auto stamp = [&](u64 entries) {
        return ShaderWarm::CurrentWarmStamp(warm_file, entries, device.GetDriverVersion(),
                                            device.GetDeviceID(), CACHE_VERSION, NoInlineBits(),
                                            LinkModeBits(device));
    };
    const auto current = stamp(0);
    const auto marker = ShaderWarm::ReadWarmMarker(warm_file);
    if (current && marker && marker->Matches(*current)) {
        LOG_INFO(Render_Vulkan, "Shader precache: graphics programs already done for this driver");
        return 0;
    }
    // Streamed: the file can be 1.5 GB; workers take the next entry under the reader's lock.
    struct Reader {
        std::mutex mutex;
        std::ifstream file;
        std::streampos end{};
        bool failed{};
        std::atomic<size_t> built{};
        std::atomic<size_t> failed_builds{};
        /// Builds the driver refused: worth another precache, unlike entries that cannot be built.
        std::atomic<size_t> driver_failures{};
        size_t read{}; ///< Entries taken from the file (under the mutex)
        std::streamoff position{};
    };
    Reader reader;
    size_t known_total = 0; ///< Entry count recorded by the builder, 0 when unknown
    try {
        reader.file.open(warm_file, std::ios::binary | std::ios::ate);
        if (!reader.file.is_open()) {
            LOG_WARNING(Render_Vulkan, "Shader precache: cannot open {}", path);
            return 0;
        }
        reader.file.exceptions(std::ifstream::failbit);
        reader.end = reader.file.tellg();
        known_total = static_cast<size_t>(ShaderWarm::ReadWarmCount(warm_file));
        reader.file.seekg(0, std::ios::beg);
        std::array<char, 8> magic{};
        u32 version{};
        reader.file.read(magic.data(), magic.size())
            .read(reinterpret_cast<char*>(&version), sizeof(version));
        constexpr std::array<char, 8> expected_magic{'y', 'u', 'z', 'u', 'c', 'a', 'c', 'h'};
        if (magic != expected_magic || version != CACHE_VERSION) {
            LOG_WARNING(Render_Vulkan, "Shader precache: {} is not a version {} file", path,
                        CACHE_VERSION);
            return 0;
        }
        reader.position = reader.file.tellg();
    } catch (const std::exception& e) {
        LOG_WARNING(Render_Vulkan, "Shader precache: opening {} failed: {}", path, e.what());
        return 0;
    }
    const auto begin = std::chrono::steady_clock::now();
    LOG_INFO(Render_Vulkan, "Shader precache: graphics programs of {} ({} MB) on {} threads", path,
             static_cast<long long>(reader.end) / (1024 * 1024), num_threads);
    std::atomic<bool> cancel{false};
    {
        std::vector<std::jthread> threads;
        for (size_t thread = 0; thread < num_threads; ++thread) {
            threads.emplace_back([this, &reader, &cancel] {
                Common::SetCurrentThreadName("VkShaderPrecache");
                static const GuessMissing guess{};
                GraphicsPipelineLibraryCache::SetNoRetainOnThisThread(true);
                ShaderPools pools;
                while (!cancel.load(std::memory_order_relaxed)) {
                    FileEnvironment env;
                    GraphicsPipelineCacheKey key{};
                    {
                        std::scoped_lock lock{reader.mutex};
                        if (reader.failed || reader.file.tellg() == reader.end) {
                            break;
                        }
                        try {
                            u32 num_envs{};
                            reader.file.read(reinterpret_cast<char*>(&num_envs),
                                             sizeof(num_envs));
                            if (num_envs != 1) {
                                LOG_WARNING(Render_Vulkan,
                                            "Shader precache: unexpected graphics entry, stopping");
                                reader.failed = true;
                                break;
                            }
                            env.Deserialize(reader.file, reader.end);
                            reader.file.read(reinterpret_cast<char*>(&key), sizeof(key));
                            ++reader.read;
                            reader.position = reader.file.tellg();
                        } catch (const std::exception& e) {
                            LOG_WARNING(Render_Vulkan, "Shader precache: graphics read failed: {}",
                                        e.what());
                            reader.failed = true;
                            break;
                        }
                    }
                    env.SetGuessMissing(&guess);
                    WarmResult result = WarmResult::Unbuildable;
                    try {
                        Shader::Environment* const env_ptr = &env;
                        result = WarmGraphicsLibraries(pools, key, std::span{&env_ptr, 1});
                    } catch (const vk::Exception& e) {
                        LOG_DEBUG(Render_Vulkan, "Shader precache: {}", e.what());
                        result = WarmResult::DriverFailed;
                    } catch (const std::exception& e) {
                        LOG_DEBUG(Render_Vulkan, "Shader precache: {}", e.what());
                    }
                    (result == WarmResult::Built ? reader.built : reader.failed_builds)
                        .fetch_add(1);
                    if (result == WarmResult::DriverFailed) {
                        reader.driver_failures.fetch_add(1);
                    }
                }
            });
        }
        while (true) {
            {
                std::scoped_lock lock{reader.mutex};
                if (reader.failed || reader.position == reader.end) {
                    break;
                }
            }
            if (stop_loading.stop_requested() || ShaderWarm::TakeSkipWarm()) {
                // Every thread stops after its current build; the scope end joins them.
                cancel = true;
                skipped = true;
                break;
            }
            if (callback) {
                const size_t done = reader.built + reader.failed_builds;
                // The builder's recorded count, or else an estimate from the bytes read.
                size_t total = known_total;
                if (total == 0) {
                    std::scoped_lock lock{reader.mutex};
                    const std::streamoff header = 12;
                    const std::streamoff end = reader.end;
                    if (reader.position > header) {
                        total = static_cast<size_t>(static_cast<double>(reader.read) *
                                                    static_cast<double>(end - header) /
                                                    static_cast<double>(reader.position - header));
                    }
                }
                callback(VideoCore::LoadCallbackStage::Warm, done_before + done,
                         total_before + std::max(total, done));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
    }
    const bool complete =
        !reader.failed && reader.end == reader.position && reader.driver_failures == 0;
    if (complete && !skipped && !stop_loading.stop_requested() &&
        SaveVulkanPipelineCacheAndWait()) {
        // A precache with driver failures writes no marker, so the next start tries again.
        if (const auto stamp_done = stamp(reader.read)) {
            ShaderWarm::WriteWarmMarker(warm_file, *stamp_done);
        }
    }
    LOG_INFO(Render_Vulkan,
             "Shader precache: graphics programs {}, {} built, {} failed ({} by the driver), "
             "{:.1f} s",
             stop_loading.stop_requested() ? "stopped" : (skipped ? "skipped" : "finished"),
             reader.built.load(), reader.failed_builds.load(), reader.driver_failures.load(),
             std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count());
    return reader.built;
}

void PipelineCache::PrecacheShaders(u64 title_id, std::vector<PrecacheJob> jobs,
                                    std::unordered_set<u64> precached, std::stop_token stop_loading,
                                    const VideoCore::DiskResourceLoadCallback& callback) {
    namespace ShaderWarm = VideoCommon::ShaderWarm;
    const size_t graphics_jobs = jobs.size();
    const std::filesystem::path warm_file = ShaderWarm::ComputeWarmPath(title_id);
    const std::string path = Common::FS::PathToUTF8String(warm_file);
    const auto stamp = [&](u64 entries) {
        return ShaderWarm::CurrentWarmStamp(warm_file, entries, device.GetDriverVersion(),
                                            device.GetDeviceID(), CACHE_VERSION, NoInlineBits());
    };
    // Compute programs of the precache file, unless a matching marker says they were built.
    size_t file_entries = 0;
    bool compute_pending = false;
    std::error_code ec;
    const auto current = stamp(0);
    const auto marker = ShaderWarm::ReadWarmMarker(warm_file);
    if (!std::filesystem::exists(warm_file, ec)) {
        LOG_INFO(Render_Vulkan, "Shader precache: no precache file at {}", path);
    } else if (current && marker && marker->Matches(*current)) {
        LOG_INFO(Render_Vulkan, "Shader precache: compute programs already done for this driver");
    } else {
        // Read the whole file here (its own reader: the disk cache loader deletes files it rejects).
        try {
            std::ifstream file(warm_file, std::ios::binary | std::ios::ate);
            file.exceptions(std::ifstream::failbit);
            const auto end{file.tellg()};
            file.seekg(0, std::ios::beg);
            std::array<char, 8> magic;
            u32 version{};
            file.read(magic.data(), magic.size())
                .read(reinterpret_cast<char*>(&version), sizeof(version));
            constexpr std::array<char, 8> expected_magic{'y', 'u', 'z', 'u', 'c', 'a', 'c', 'h'};
            if (magic != expected_magic || version != CACHE_VERSION) {
                LOG_WARNING(Render_Vulkan, "Shader precache: {} is not a version {} file", path,
                            CACHE_VERSION);
                throw std::runtime_error("wrong version");
            }
            while (file.tellg() != end) {
                u32 num_envs{};
                file.read(reinterpret_cast<char*>(&num_envs), sizeof(num_envs));
                FileEnvironment env;
                env.Deserialize(file, end);
                if (num_envs != 1 || env.ShaderStage() != Shader::Stage::Compute) {
                    LOG_WARNING(Render_Vulkan, "Shader precache: unexpected entry in {}", path);
                    throw std::runtime_error("unexpected entry");
                }
                ComputePipelineCacheKey key;
                file.read(reinterpret_cast<char*>(&key), sizeof(key));
                ++file_entries;
                // Programs already in the disk cache were built above; leave them out.
                if (compute_cache.contains(key) || build_journal->Skip(key.Hash())) {
                    continue;
                }
                const u64 cpu_bytes = TranslationBytes(env.MemorySize());
                jobs.push_back({
                    .journal_id = key.Hash(),
                    .translate = [this, key,
                                  env = std::move(env)](VideoCommon::ShaderJobContext&) mutable
                        -> Common::UniqueFunction<bool> {
                        static const GuessMissing guess{};
                        env.SetGuessMissing(&guess);
                        ShaderPools pools;
                        auto pipeline =
                            CreateComputePipeline(pools, key, env, nullptr, false).pipeline;
                        if (!pipeline) {
                            return {};
                        }
                        return [pipeline = std::move(pipeline)] {
                            pipeline->Build();
                            return true;
                        };
                    },
                    .cpu_bytes = cpu_bytes,
                });
            }
            compute_pending = true;
        } catch (const std::exception& e) {
            LOG_WARNING(Render_Vulkan, "Shader precache: reading {} failed: {}", path, e.what());
            jobs.resize(graphics_jobs);
            file_entries = 0;
        }
    }
    const bool graphics_warm_pending = [&] {
        if (!device.IsExtGraphicsPipelineLibrarySupported()) {
            return false;
        }
        const auto graphics_file = ShaderWarm::GraphicsWarmPath(title_id);
        if (!std::filesystem::exists(graphics_file, ec)) {
            return false;
        }
        const auto graphics_current = ShaderWarm::CurrentWarmStamp(
            graphics_file, 0, device.GetDriverVersion(), device.GetDeviceID(), CACHE_VERSION,
            NoInlineBits(), LinkModeBits(device));
        const auto graphics_marker = ShaderWarm::ReadWarmMarker(graphics_file);
        return !(graphics_current && graphics_marker &&
                 graphics_marker->Matches(*graphics_current));
    }();
    if (jobs.empty() && !compute_pending && !graphics_warm_pending && !mesh_warm_enabled) {
        LOG_INFO(Render_Vulkan, "Shader precache: nothing to precache");
        return;
    }

    const auto begin = std::chrono::steady_clock::now();
    // The cached pipelines and compute programs build as shader jobs (the count is for the log);
    // the graphics warm-up below runs on PrecacheThreads() threads of its own.
    const size_t num_threads = GetTotalPipelineWorkers();
    std::vector<u8> built(jobs.size());
    std::atomic<size_t> done{};
    bool skipped = false;
    LOG_INFO(Render_Vulkan,
             "Shader precache: {} cached graphics pipelines and {} of {} compute programs to "
             "build on {} threads",
             graphics_jobs, jobs.size() - graphics_jobs, file_entries, num_threads);
    VideoCommon::StutterTrace::BootMarker("precache_begin", fmt::format("jobs={}", jobs.size()));
    if (!jobs.empty()) {
        // Background jobs of the shader workers, each built and destroyed: only the driver's
        // cache keeps it.
        const VideoCommon::ShaderJobBatch batch = shader_jobs->CreateBatch();
        for (size_t index = 0; index < jobs.size(); ++index) {
            shader_jobs->Submit(
                {
                    .priority = VideoCommon::ShaderJobPriority::Background,
                    .cpu_bytes = jobs[index].cpu_bytes,
                    .driver_bytes = DriverCompileBytes,
                    .cpu = [&built, &done, index, id = jobs[index].journal_id, this,
                            translate = std::move(jobs[index].translate)](
                               VideoCommon::ShaderJobContext& context) mutable
                        -> VideoCommon::ShaderDriverStage {
                        Common::UniqueFunction<bool> build;
                        if (!context.StopRequested()) {
                            try {
                                build = translate(context);
                            } catch (const std::exception& e) {
                                LOG_DEBUG(Render_Vulkan, "Shader precache: {}", e.what());
                            }
                        }
                        translate = {};
                        if (!build) {
                            done.fetch_add(1);
                            return {};
                        }
                        return
                            [&built, &done, index, id, this, build = std::move(build)]() mutable {
                                try {
                                    const DriverBuildJournal::Scope recorded{&*build_journal, id};
                                    built[index] = build();
                                } catch (const std::exception& e) {
                                    LOG_DEBUG(Render_Vulkan, "Shader precache: {}", e.what());
                                }
                                done.fetch_add(1);
                            };
                    },
                },
                &batch);
        }
        while (!batch.Idle()) {
            if (stop_loading.stop_requested() || ShaderWarm::TakeSkipWarm()) {
                // The builds not started are dropped; the running ones end first.
                shader_jobs->Cancel(batch);
                skipped = true;
                break;
            }
            if (callback) {
                callback(VideoCore::LoadCallbackStage::Warm, done, jobs.size());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
        batch.Wait();
    }
    const size_t total_built = static_cast<size_t>(std::count(built.begin(), built.end(), u8{1}));
    const bool cache_ready = !skipped && !stop_loading.stop_requested() &&
                             SaveVulkanPipelineCacheAndWait();
    const bool compute_ok = std::all_of(built.begin() + graphics_jobs, built.end(),
                                      [](u8 value) { return value != 0; });
    if (compute_pending && compute_ok && cache_ready) {
        if (const auto stamp_done = stamp(file_entries)) {
            ShaderWarm::WriteWarmMarker(warm_file, *stamp_done);
        }
    }
    const size_t precached_before = precached.size();
    for (size_t index = 0; index < graphics_jobs; ++index) {
        if (built[index]) {
            precached.insert(jobs[index].graphics_hash);
        }
    }
    if (precached.size() != precached_before) {
        ShaderWarm::WritePrecachedPipelines(
            ShaderWarm::PrecachedPipelinesPath(title_id),
            {device.GetDriverVersion(), device.GetDeviceID(), CACHE_VERSION}, precached);
    }
    if (!stop_loading.stop_requested() && total_built != 0) {
        // Without this, the builds reach the file only at the next boot, game change or clean
        // close, and are lost if the app ends otherwise.
        QueueVulkanPipelineCacheSave();
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    LOG_INFO(Render_Vulkan, "Shader precache: {}, {} built, {} failed, {} not reached, {:.1f} s",
             stop_loading.stop_requested() ? "stopped" : (skipped ? "skipped" : "finished"),
             total_built, done - total_built, jobs.size() - std::min(jobs.size(), done.load()),
             seconds);
    VideoCommon::StutterTrace::BootMarker(
        "precache_end", fmt::format("built={} seconds={:.3f}", total_built, seconds));
    // The graphics programs of the game files come last, on threads of their own
    // (shader_warm_threads / EDEN_NX2_WARM_THREADS).
    if (graphics_warm_pending && !skipped && !stop_loading.stop_requested()) {
        const size_t graphics_built =
            PrecacheGraphicsWarm(title_id, PrecacheThreads(), done.load(), jobs.size(),
                                 stop_loading, callback, skipped);
        if (!stop_loading.stop_requested() && graphics_built != 0) {
            QueueVulkanPipelineCacheSave();
        }
    }
    if (!skipped && !stop_loading.stop_requested()) {
        PrecacheMeshWarm(title_id, stop_loading, callback, skipped);
    }
}

void PipelineCache::PrecacheMeshWarm(u64 title_id, std::stop_token stop_loading,
                                    const VideoCore::DiskResourceLoadCallback& callback,
                                    bool& skipped) {
    namespace ShaderWarm = VideoCommon::ShaderWarm;
    if (!mesh_warm_enabled || !device.IsExtMeshShaderSupported()) {
        return;
    }
    const auto warm_file = [&] {
        const char* override = std::getenv("EDEN_NX2_MESH_WARM");
        return override && *override ? std::filesystem::path{override}
                                     : ShaderWarm::MeshWarmPath(title_id);
    }();
    std::error_code error;
    if (!std::filesystem::exists(warm_file, error)) {
        return;
    }
    const auto stamp = [&](u64 entries) {
        auto value = ShaderWarm::CurrentWarmStamp(
            warm_file, entries, device.GetDriverVersion(), device.GetDeviceID(), CACHE_VERSION,
            NoInlineBits(), LinkModeBits(device));
        if (value) {
            value->configuration = fmt::format(
                "mesh4-{}-{}-{}-{}-{}-{}-{}-{}", u32(device.GetDriverID()),
                profile.supported_spirv, profile.need_fastmath_off,
                profile.ignore_nan_fp_comparisons, profile.support_native_ndc,
                profile.support_cooperative_hmma, profile.support_cooperative_hmma_16x16x16,
                device.IsExtGraphicsPipelineLibrarySupported());
        }
        return value;
    };
    const auto initial = stamp(0);
    const auto marker = ShaderWarm::ReadWarmMarker(warm_file);
    if ((device.DriverCachesPipelines() ||
         (use_vulkan_pipeline_cache &&
          std::filesystem::exists(vulkan_pipeline_cache_filename, error))) &&
        initial && marker && marker->Matches(*initial)) {
        return;
    }
    size_t built{}, rejected{}, driver_failed{}, read{};
    bool complete{};
    const auto cancelled = [&] {
        skipped = skipped || ShaderWarm::TakeSkipWarm();
        return skipped || stop_loading.stop_requested();
    };
    try {
        std::ifstream file(warm_file, std::ios::binary | std::ios::ate);
        file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        const std::streamoff end = file.tellg();
        file.seekg(0);
        std::array<char, 8> magic{};
        u32 version{};
        file.read(magic.data(), magic.size());
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        constexpr std::array<char, 8> expected{'y', 'u', 'z', 'u', 'c', 'a', 'c', 'h'};
        if (magic != expected || version != CACHE_VERSION) {
            throw std::runtime_error("Unsupported mesh precache file header");
        }
        const size_t total = ShaderWarm::ReadWarmCount(warm_file);
        while (!cancelled() && file.tellg() < end) {
            u32 count{};
            file.read(reinterpret_cast<char*>(&count), sizeof(count));
            if (count != 2 && count != 3) {
                throw std::runtime_error("Mesh precache record needs two or three stages");
            }
            std::array<FileEnvironment, 3> environments;
            u64 budget = ShaderWarm::MaxMeshWarmRecordBytes -
                         sizeof(GraphicsPipelineCacheKey) - sizeof(count);
            for (u32 i = 0; i < count; ++i) {
                const u32 stage = i + 1 == count ? 4 : (count == 3 && i == 0 ? 7 : 8);
                const std::streamoff offset = file.tellg();
                const u64 bytes = ShaderWarm::InspectMeshWarmEnvironment(
                    file, stage, std::min(budget, static_cast<u64>(end - offset)));
                budget -= bytes;
                file.seekg(offset);
                environments[i].Deserialize(file, end);
                if (std::streamoff{file.tellg()} != offset + static_cast<std::streamoff>(bytes) ||
                    !environments[i].IsNativeProgram()) {
                    throw std::runtime_error("Invalid native mesh precache environment");
                }
            }
            if (count == 3 && environments[0].MeshRegisters() != environments[1].MeshRegisters()) {
                throw std::runtime_error("Inconsistent task and mesh register context");
            }
            GraphicsPipelineCacheKey key{};
            file.read(reinterpret_cast<char*>(&key), sizeof(key));
            if (!ShaderWarm::ValidMeshWarmKey(key.unique_hashes, count)) {
                throw std::runtime_error("Invalid mesh precache stage slots");
            }
            ++read;
            if (!KeyFitsDevice(key) || build_journal->Skip(key.Hash())) {
                ++rejected;
                continue;
            }
            // One detached candidate at a time, charged to the same driver budget as native
            // mesh draws. All descriptor banks and libraries die before the job completes.
            auto batch = shader_jobs->CreateBatch();
            shader_jobs->Submit(
                {.priority = VideoCommon::ShaderJobPriority::Background,
                 .driver_bytes = GraphicsDriverBytes(true),
                 .driver = [this, key, count, &built, &rejected, &driver_failed,
                            environments = std::move(environments)]() mutable {
                     GraphicsPipelineLibraryCache::SetNoRetainOnThisThread(true);
                     SCOPE_EXIT { GraphicsPipelineLibraryCache::SetNoRetainOnThisThread(false); };
                     try {
                         static const GuessMissing guess{};
                         std::array<Shader::Environment*, 3> pointers{};
                         for (u32 i = 0; i < count; ++i) {
                             environments[i].SetGuessMissing(&guess);
                             pointers[i] = &environments[i];
                         }
                         ShaderPools pools;
                         DescriptorPool descriptors{device, scheduler};
                         auto pipeline = CreateGraphicsPipeline(
                             pools, key, std::span{pointers.data(), count}, nullptr, false,
                             nullptr, nullptr, false, false, &descriptors);
                         if (!pipeline) {
                             ++rejected;
                             return;
                         }
                         const DriverBuildJournal::Scope recorded{&*build_journal, key.Hash()};
                         pipeline->Build();
                         if (pipeline->Failed()) {
                             ++driver_failed;
                         } else {
                             ++built;
                         }
                     } catch (const vk::Exception& exception) {
                         ++driver_failed;
                         LOG_DEBUG(Render_Vulkan, "mesh precache: {}", exception.what());
                     } catch (const std::exception& exception) {
                         ++rejected;
                         LOG_DEBUG(Render_Vulkan, "mesh precache: {}", exception.what());
                     }
                 }},
                &batch);
            while (!batch.Idle()) {
                if (cancelled()) {
                    shader_jobs->Cancel(batch);
                    break;
                }
                if (callback) {
                    callback(VideoCore::LoadCallbackStage::Warm, read - 1,
                             std::max(total, read));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
            }
            batch.Wait();
        }
        complete = file.tellg() == end && !cancelled() && driver_failed == 0 && rejected == 0;
    } catch (const std::exception& exception) {
        LOG_WARNING(Render_Vulkan, "Mesh precache stopped: {}", exception.what());
    }
    const bool cache_ready = complete && SaveVulkanPipelineCacheAndWait();
    const auto final = stamp(read);
    if (cache_ready && !cancelled() && initial && final && initial->Matches(*final)) {
        ShaderWarm::WriteWarmMarker(warm_file, *final);
    }
    LOG_INFO(Render_Vulkan, "Mesh precache: {} built, {} rejected, {} driver failures, complete={}",
             built, rejected, driver_failed, complete && cache_ready);
}

bool PipelineCache::SaveVulkanPipelineCacheAndWait() {
    if (device.DriverCachesPipelines()) {
        return true;
    }
    if (!use_vulkan_pipeline_cache || vulkan_pipeline_cache_filename.empty()) {
        return false;
    }
    auto saved = std::make_shared<std::promise<bool>>();
    auto result = saved->get_future();
    serialization_thread.QueueWork([this, saved] {
        try {
            saved->set_value(SerializeVulkanPipelineCache(
                vulkan_pipeline_cache_filename, vulkan_pipeline_cache, CACHE_VERSION));
        } catch (...) {
            saved->set_exception(std::current_exception());
        }
    });
    try {
        return result.get();
    } catch (const std::exception& exception) {
        LOG_WARNING(Render_Vulkan, "Shader precache cache save failed: {}", exception.what());
        return false;
    }
}

void PipelineCache::CanonicalizeNativeKey(
    GraphicsPipelineCacheKey& key,
    std::span<const Shader::Environment* const> envs) const noexcept {
    if (envs.empty() || !std::ranges::all_of(envs, [](const Shader::Environment* env) {
            return env->IsNativeProgram();
        })) {
        return;
    }
    u32 mask = ~0U;
    for (const Shader::Environment* const env : envs) {
        if (env->ShaderStage() == Shader::Stage::Task || env->ShaderStage() == Shader::Stage::Mesh) {
            mask = 0;
            break;
        }
        if (env->ShaderStage() == Shader::Stage::VertexB &&
            !IsUnmaskedVertexProgram(key.unique_hashes[1])) {
            mask = VideoCommon::GenericReadMask(env->SPH());
        }
    }
    key.state.CanonicalizeNative(mask);
}

namespace {
/// Whether the driver keeps compiled pipelines in a cache of its own (the proprietary NVIDIA and
/// AMD drivers), where the preparation step leaves them.
bool DriverCachesPipelines(const Device& device) {
    switch (device.GetDriverID()) {
    case VK_DRIVER_ID_NVIDIA_PROPRIETARY:
    case VK_DRIVER_ID_AMD_PROPRIETARY:
        return true;
    default:
        return false;
    }
}
} // Anonymous namespace

namespace {
/// Rechecked on a pipeline miss, so live settings changes cannot draw old prepared translations.
std::string PreparedTranslationPolicy() {
    std::string result;
    const auto add = [&](auto value) { result += fmt::format(" {}", value); };
    add(Settings::GetCurrentProgramID());
    add(Settings::values.resolution_info.active);
    add(Settings::values.resolution_info.up_scale);
    add(Settings::values.resolution_info.down_shift);
    add(Settings::values.resolution_info.up_factor);
    add(Settings::values.resolution_info.down_factor);
    add(Settings::values.disable_shader_loop_safety_checks.GetValue());
    add(Settings::values.fix_bloom_effects.GetValue());
    add(Settings::values.renderer_debug.GetValue());
    add(static_cast<u32>(Settings::values.tensor_programs.GetValue()));
    add(Settings::values.shader_calls_not_inlined.GetValue());
    add(VideoCommon::OotDlss::Requested());
    add(VideoCommon::OotDlss::ObjectMotionRequested());
    for (const char* name : {"EDEN_NX2_UNDEF_ZERO", "EDEN_NX2_NO_TENSOR",
                             "EDEN_NX2_GRAPHICS_NOINLINE", "EDEN_NX2_COMPUTE_NOINLINE",
                             "EDEN_OOT_MESH_SIGNATURE", "EDEN_OOT_MESH_MOTION",
                             "EDEN_OOT_OBJECT_MOTION", "LICHII_BINDLESS_TABLE"}) {
        const char* value = std::getenv(name);
        result += fmt::format(" {}={}", name, value ? value : "<unset>");
    }
    return result;
}
} // namespace

std::string PipelineCache::NativePreparationStamp(const std::filesystem::path& path) const {
    // The file is assembled locally from the player's game files; foreign caches are not read.
    const auto content = VideoCommon::ShaderWarm::ContentHash(path);
    if (content.empty()) {
        return {};
    }
    const auto properties = device.GetPhysical().GetProperties();
    std::string stamp = fmt::format(
        "lichii-2 {} {} {} {} {} {} {}", content, LICHII_PREPARATION_REVISION, CACHE_VERSION,
        properties.vendorID, properties.deviceID, properties.driverVersion, properties.apiVersion);
    for (const auto byte : properties.pipelineCacheUUID) {
        stamp += fmt::format("{:02x}", byte);
    }
    const auto setting = [&](auto value) { stamp += fmt::format(" {}", value); };
    setting(profile.supported_spirv);
    setting(profile.unified_descriptor_binding);
    setting(profile.support_descriptor_aliasing);
    setting(profile.support_int8);
    setting(profile.support_uniform_and_storage_buffer_8bit);
    setting(profile.support_storage_buffer_8bit);
    setting(profile.support_int16);
    setting(profile.support_uniform_and_storage_buffer_16bit);
    setting(profile.support_storage_buffer_16bit);
    setting(profile.support_int64);
    setting(profile.support_vertex_instance_id);
    setting(profile.support_float_controls);
    setting(profile.support_separate_denorm_behavior);
    setting(profile.support_separate_rounding_mode);
    setting(profile.support_fp16_denorm_preserve);
    setting(profile.support_fp32_denorm_preserve);
    setting(profile.support_fp16_denorm_flush);
    setting(profile.support_fp32_denorm_flush);
    setting(profile.support_fp16_signed_zero_nan_preserve);
    setting(profile.support_fp32_signed_zero_nan_preserve);
    setting(profile.support_fp64_signed_zero_nan_preserve);
    setting(profile.support_explicit_workgroup_layout);
    setting(profile.support_workgroup_layout_8bit_access);
    setting(profile.support_workgroup_layout_16bit_access);
    setting(profile.support_shader_quad_control);
    setting(profile.support_quad_shuffles);
    setting(profile.support_vote);
    setting(profile.support_shuffle_relative);
    setting(profile.supported_subgroup_stages);
    setting(profile.support_viewport_index_layer_non_geometry);
    setting(profile.support_viewport_mask);
    setting(profile.support_typeless_image_loads);
    setting(profile.support_demote_to_helper_invocation);
    setting(profile.support_int64_atomics);
    setting(profile.support_shared_int64_atomics);
    setting(profile.support_derivative_control);
    setting(profile.support_geometry_shader_passthrough);
    setting(profile.support_native_ndc);
    setting(profile.support_gl_nv_gpu_shader_5);
    setting(profile.support_gl_amd_gpu_shader_half_float);
    setting(profile.support_gl_texture_shadow_lod);
    setting(profile.support_gl_warp_intrinsics);
    setting(profile.support_gl_variable_aoffi);
    setting(profile.support_gl_sparse_textures);
    setting(profile.support_gl_derivative_control);
    setting(profile.support_scaled_attributes);
    setting(profile.support_multi_viewport);
    setting(profile.support_geometry_streams);
    setting(profile.support_sampled_image_array_nonuniform_indexing);
    setting(profile.support_storage_image_array_nonuniform_indexing);
    setting(profile.support_uniform_texel_buffer_array_nonuniform_indexing);
    setting(profile.support_storage_texel_buffer_array_nonuniform_indexing);
    setting(profile.warp_size_potentially_larger_than_guest);
    setting(profile.lower_left_origin_mode);
    setting(profile.need_declared_frag_colors);
    setting(profile.need_fastmath_off);
    setting(profile.need_gather_subpixel_offset);
    setting(profile.has_broken_spirv_clamp);
    setting(profile.has_broken_spirv_position_input);
    setting(profile.has_broken_unsigned_image_offsets);
    setting(profile.has_broken_signed_operations);
    setting(profile.has_broken_fp16_float_controls);
    setting(profile.max_compute_shared_memory_size);
    setting(profile.has_broken_fp32_denorm_flush);
    setting(profile.has_gl_component_indexing_bug);
    setting(profile.has_gl_precise_bug);
    setting(profile.has_gl_cbuf_ftou_bug);
    setting(profile.has_gl_bool_ref_bug);
    setting(profile.ignore_nan_fp_comparisons);
    setting(profile.has_broken_spirv_subgroup_mask_vector_extract_dynamic);
    setting(profile.has_broken_spirv_subgroup_shuffle);
    setting(profile.has_broken_spirv_fragment_shuffle);
    setting(profile.support_cooperative_hmma);
    setting(profile.support_cooperative_hmma_16x16x16);
    setting(profile.support_cooperative_hmma_8x16x16);
    setting(profile.max_subgroup_size);
    setting(profile.gl_max_compute_smem_size);
    setting(profile.has_broken_robust);
    setting(profile.min_ssbo_alignment);
    setting(profile.max_user_clip_distances);
    setting(host_info.min_ssbo_alignment);
    setting(host_info.max_per_stage_descriptor_sampled_images);
    setting(host_info.max_per_stage_resources);
    setting(host_info.max_per_stage_descriptor_storage_buffers);
    setting(host_info.max_per_stage_descriptor_samplers);
    setting(host_info.max_descriptor_set_samplers);
    setting(host_info.max_descriptor_set_uniform_buffers);
    setting(host_info.max_descriptor_set_uniform_buffers_dynamic);
    setting(host_info.max_descriptor_set_storage_buffers);
    setting(host_info.max_descriptor_set_storage_buffers_dynamic);
    setting(host_info.max_descriptor_set_sampled_images);
    setting(host_info.max_descriptor_set_storage_images);
    setting(host_info.max_descriptor_set_input_attachements);
    setting(host_info.support_float64);
    setting(host_info.support_float16);
    setting(host_info.support_int64);
    setting(host_info.needs_demote_reorder);
    setting(host_info.support_snorm_render_buffer);
    setting(host_info.support_viewport_index_layer);
    setting(host_info.support_viewport_mask);
    setting(host_info.support_geometry_shader_passthrough);
    setting(host_info.support_conditional_barrier);
    setting(host_info.single_lane_geometry_subgroups);
    setting(host_info.support_byte_addressed_r16);
    setting(host_info.needs_lrz_disable);
    setting(host_info.support_handle_table_arrays);
    setting(Settings::values.resolution_info.active);
    setting(Settings::values.resolution_info.up_scale);
    setting(Settings::values.resolution_info.down_shift);
    setting(Settings::values.resolution_info.up_factor);
    setting(Settings::values.resolution_info.down_factor);
    setting(Settings::values.disable_shader_loop_safety_checks.GetValue());
    setting(Settings::values.fix_bloom_effects.GetValue());
    setting(Settings::values.renderer_debug.GetValue());
    for (const char* name : {"EDEN_NX2_UNDEF_ZERO", "EDEN_NX2_NO_TENSOR"}) {
        const char* value = std::getenv(name);
        stamp += fmt::format(" {}={}", name, value ? value : "<unset>");
    }
    return stamp + PreparedTranslationPolicy();
}

void PipelineCache::PreparePrecompiled(const std::filesystem::path& base_dir, bool prepare,
                                       std::stop_token stop_loading,
                                       const VideoCore::DiskResourceLoadCallback& callback) {
    namespace Warm = VideoCommon::ShaderWarm;
    auto path = base_dir / "native-preparation.bin";
    std::error_code exists_error;
    if (!std::filesystem::exists(path, exists_error)) {
        path = base_dir / fmt::format("precompiled.fork-v{}.nx2prep", CACHE_VERSION);
    }
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return;
    }
    const char* enabled = std::getenv("EDEN_NX2_PRECOMPILED");
    if (enabled && enabled[0] == '0') {
        return;
    }
    const std::string stamp = NativePreparationStamp(path);
    const auto infos_path = base_dir / "native-prepared.journal";
    if (const char* force = std::getenv("EDEN_NX2_PRECOMPILED_FORCE"); force && force[0] == '1') {
        std::filesystem::remove(infos_path, error);
    }
    Warm::PreparedStore store;
    if (stamp.empty() || !store.Open(infos_path, stamp)) {
        LOG_WARNING(Render_Vulkan, "Cannot open the native shader preparation journal");
        return;
    }
    // Without a driver cache of its own the prepared pipelines would only warm this session.
    if (prepare && !DriverCachesPipelines(device) && !(enabled && enabled[0] == '1')) {
        LOG_INFO(Render_Vulkan, "Native shader preparation skipped: the driver keeps no pipeline "
                                "cache of its own (EDEN_NX2_PRECOMPILED=1 prepares anyway)");
        prepare = false;
    }
    LoadPrecompiledInfos(path, infos_path, store);
    if (prepare) {
        const auto journal_size = std::filesystem::file_size(infos_path, error);
        RunNativePreparation(path, store, stop_loading, callback);
        const auto updated_size = std::filesystem::file_size(infos_path, error);
        if (error || updated_size != journal_size) {
            LoadPrecompiledInfos(path, infos_path, store);
        }
    }
}

void PipelineCache::RunNativePreparation(const std::filesystem::path& path,
                                         VideoCommon::ShaderWarm::PreparedStore& store,
                                         std::stop_token stop_loading,
                                         const VideoCore::DiskResourceLoadCallback& callback) {
    namespace Warm = VideoCommon::ShaderWarm;
    std::unordered_set<u32> done_before;
    done_before = precompiled_record_ids;
    std::ifstream file(path, std::ios::binary);
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    const auto get = [&file](auto& value) {
        file.read(reinterpret_cast<char*>(&value), sizeof(value));
    };
    struct {
        std::mutex mutex;
        std::condition_variable_any cv;
        size_t in_flight{};
        size_t done{};
        size_t built{};
        size_t failed{};
        size_t incompatible{};
    } state;
    std::atomic<bool> cancel{};
    const auto batch = shader_jobs->CreateBatch();
    // Each worker holds one pipeline at a time; it is dropped once its translation is kept.
    const size_t max_in_flight = std::max<size_t>(GetTotalPipelineWorkers() * 2, 4);
    const auto begin = std::chrono::steady_clock::now();
    u32 count{};
    try {
        const u64 file_size = std::filesystem::file_size(path);
        std::array<char, 8> magic{};
        u32 version{}, env_count{}, reserved{};
        get(magic);
        get(version);
        get(env_count);
        get(count);
        get(reserved);
        // Layout (tools/nx2-precompile/README.md): "NX2PREP1", u32 cache version, u32 environment
        // count, u32 record count, u32 reserved; per environment u64 offset and u64 size; per
        // record u8 kind (0 graphics, 1 compute), u8 environment count, u16 reserved, u32
        // environment indices in stage order, then the key.
        if (std::string_view(magic.data(), magic.size()) != "NX2PREP1" ||
            file_size < 24 || version != CACHE_VERSION || reserved != 0 ||
            env_count > Warm::PreparedStore::MaxEntries ||
            count > Warm::PreparedStore::MaxEntries || env_count > (file_size - 24) / 16) {
            throw std::runtime_error("invalid native preparation header");
        }
        auto env_index = std::make_shared<std::vector<std::pair<u64, u64>>>(env_count);
        for (auto& [offset, size] : *env_index) {
            get(offset);
            get(size);
            if (offset > file_size || size > file_size - offset ||
                size > Warm::PreparedStore::MaxPayloadBytes) {
                throw std::runtime_error("invalid native preparation environment");
            }
        }
        LOG_INFO(Render_Vulkan, "Preparing native pipelines: {} records, {} already prepared",
                 count, done_before.size());
        callback(VideoCore::LoadCallbackStage::Warm, 0, count);
        const auto report = [&] {
            size_t done;
            {
                std::scoped_lock lock{state.mutex};
                done = state.done;
            }
            callback(VideoCore::LoadCallbackStage::Warm, done, count);
        };
        for (u32 index = 0; index < count; ++index) {
            if (stop_loading.stop_requested() || Warm::SkipWarmRequested()) {
                cancel = true;
                break;
            }
            u8 kind{}, env_total{};
            u16 pad{};
            get(kind);
            get(env_total);
            get(pad);
            if (kind > 1 || env_total == 0 || env_total > 5 || (kind == 1 && env_total != 1) ||
                pad != 0) {
                throw std::runtime_error("invalid native preparation record");
            }
            auto record = std::make_shared<PrecompiledRecord>();
            record->env_count = env_total;
            record->envs.fill(NoInfo);
            for (u8 i = 0; i < env_total; ++i) {
                get(record->envs[i]);
                if (record->envs[i] >= env_count) {
                    throw std::runtime_error("invalid native preparation environment index");
                }
            }
            GraphicsPipelineCacheKey graphics{};
            ComputePipelineCacheKey compute{};
            if (kind == 1) {
                get(compute);
            } else {
                get(graphics);
            }
            bool skip = done_before.contains(index);
            if (!skip && kind == 0) {
                ClearDriverFields(graphics);
                const bool other_device = !KeyFitsDevice(graphics);
                if (other_device) {
                    std::scoped_lock lock{state.mutex};
                    ++state.incompatible;
                    skip = true;
                }
            }
            if (skip) {
                std::scoped_lock lock{state.mutex};
                ++state.done;
                continue;
            }
            {
                std::unique_lock lock{state.mutex};
                if (!state.cv.wait(lock, stop_loading,
                                   [&] { return state.in_flight < max_in_flight; })) {
                    cancel = true;
                    break;
                }
                ++state.in_flight;
            }
            u64 captured_bytes{};
            for (u8 i = 0; i < record->env_count; ++i) {
                captured_bytes += (*env_index)[record->envs[i]].second;
            }
            const auto finish = [&state](bool ok) {
                std::scoped_lock lock{state.mutex};
                --state.in_flight;
                ++state.done;
                ++(ok ? state.built : state.failed);
                state.cv.notify_all();
            };
            shader_jobs->Submit({
                .priority = VideoCommon::ShaderJobPriority::Soon,
                .cpu_bytes = TranslationBytes(captured_bytes),
                .driver_bytes = DriverCompileBytes,
                .cpu = [this, &path, &store, &cancel, &stop_loading, env_index,
                        record, index, kind, graphics, compute, finish](
                           VideoCommon::ShaderJobContext& context) -> VideoCommon::ShaderDriverStage {
                    if (cancel || stop_loading.stop_requested() || context.StopRequested()) {
                        finish(false);
                        return {};
                    }
                    Common::UniqueFunction<bool> build;
                    try {
                        build = PrepareNativeRecord(path, *env_index, store, index, kind == 1,
                                                    *record, graphics, compute, context);
                    } catch (const std::exception& e) {
                        LOG_DEBUG(Render_Vulkan, "Native preparation record {}: {}", index, e.what());
                    } catch (...) {
                        LOG_DEBUG(Render_Vulkan, "Native preparation record {} failed", index);
                    }
                    if (!build) {
                        finish(false);
                        return {};
                    }
                    return [build = std::move(build), finish]() mutable {
                        bool ok = false;
                        try {
                            ok = build();
                        } catch (const std::exception& e) {
                            LOG_DEBUG(Render_Vulkan, "Native preparation driver build: {}", e.what());
                        } catch (...) {
                            LOG_DEBUG(Render_Vulkan, "Native preparation driver build failed");
                        }
                        finish(ok);
                    };
                }}, &batch);
            if (index % 64 == 0) {
                report();
            }
        }
        // The queued jobs reference this frame: wait for all of them, stopped or not.
        batch.Wait();
        report();
    } catch (const std::exception& e) {
        cancel = true;
        batch.Wait();
        LOG_WARNING(Render_Vulkan, "Native shader preparation stopped: {}", e.what());
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    LOG_INFO(Render_Vulkan,
             "Native shader preparation{}: {} of {} records done ({} built now, {} prepared "
             "before, {} failed, {} for another device) in {:.1f} s; completed entries are kept",
             cancel ? " stopped" : "", state.done, count, state.built, done_before.size(),
             state.failed, state.incompatible, seconds);
}

Common::UniqueFunction<bool> PipelineCache::PrepareNativeRecord(const std::filesystem::path& path,
                                        std::span<const std::pair<u64, u64>> env_index,
                                        VideoCommon::ShaderWarm::PreparedStore& store, u32 index,
                                        bool compute, const PrecompiledRecord& record,
                                        const GraphicsPipelineCacheKey& graphics,
                                        const ComputePipelineCacheKey& compute_key,
                                        VideoCommon::ShaderJobContext& context) {
    namespace Warm = VideoCommon::ShaderWarm;
    std::ifstream file(path, std::ios::binary);
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    std::vector<FileEnvironment> environments(record.env_count);
    boost::container::static_vector<Shader::Environment*, Maxwell::MaxShaderProgram> pointers;
    for (size_t i = 0; i < record.env_count; ++i) {
        const auto [offset, size] = env_index[record.envs[i]];
        file.seekg(static_cast<std::streamoff>(offset));
        environments[i].Deserialize(file, static_cast<std::streamoff>(offset + size));
        pointers.push_back(&environments[i]);
    }
    ShaderPools pools;
    std::array<std::vector<u32>, Maxwell::MaxShaderStage> spirv;
    std::array<std::vector<u8>, 5> infos;
    struct PreparedPipelines {
        PreparedPipelines(const Device& device_, Scheduler& scheduler_) : descriptors{device_, scheduler_} {}
        // The pipelines must be destroyed before the banks their allocators reference.
        DescriptorPool descriptors;
        std::unique_ptr<ComputePipeline> compute;
        std::unique_ptr<GraphicsPipeline> graphics;
    };
    auto pipelines = std::make_unique<PreparedPipelines>(device, scheduler);
    if (compute) {
        auto entry =
            CreateComputePipeline(pools, compute_key, environments[0], nullptr, false, nullptr,
                                  &spirv[0], &pipelines->descriptors);
        if (!entry.pipeline || entry.pipeline->BuildFailed()) {
            throw std::runtime_error("compute pipeline build failed");
        }
        Shader::SerializeInfo(entry.pipeline->StageInfo(), infos[0]);
        pipelines->compute = std::move(entry.pipeline);
    } else {
        auto pipeline = CreateGraphicsPipeline(pools, graphics, MakeSpan(pointers), nullptr,
                                               false, nullptr, nullptr, false, false,
                                               &pipelines->descriptors, &spirv);
        if (!pipeline || pipeline->Failed()) {
            throw std::runtime_error("graphics pipeline build failed");
        }
        // Emulated mesh pipelines are warmed in the driver but always translated at runtime.
        if (pipeline->IsEmulatedMesh()) {
            return {};
        }
        for (size_t stage = 0; stage < infos.size(); ++stage) {
            if (const auto* const info = pipeline->StageInfo(stage)) {
                Shader::SerializeInfo(*info, infos[stage]);
            }
        }
        context.SetDriverBytes(GraphicsDriverBytes(pipeline->IsMeshPipeline()));
        pipelines->graphics = std::move(pipeline);
    }
    // A translation that asked what the recipe did not record is not one runtime can reproduce.
    for (const auto& env : environments) {
        if (env.LacksRecordedValue(host_info.EnvironmentReadProperties())) {
            throw std::runtime_error("recipe lacks translation inputs");
        }
    }
    return [&store, record, index, compute, graphics, compute_key, infos = std::move(infos),
            spirv = std::move(spirv), pipelines = std::move(pipelines)]() mutable {
    if (pipelines->compute) {
        pipelines->compute->Build();
        if (pipelines->compute->BuildFailed()) {
            return false;
        }
    } else {
        pipelines->graphics->Build();
        if (pipelines->graphics->Failed()) {
            return false;
        }
    }
    std::vector<u8> payload;
    const auto put = [&payload](const auto& value) {
        const auto* const bytes = reinterpret_cast<const u8*>(&value);
        payload.insert(payload.end(), bytes, bytes + sizeof(value));
    };
    put(static_cast<u8>(compute ? 1 : 0));
    put(record.env_count);
    put(u16{0});
    put(record.envs);
    if (compute) {
        put(compute_key);
    } else {
        put(graphics);
    }
    for (size_t stage = 0; stage < infos.size(); ++stage) {
        if (infos[stage].empty() != spirv[stage].empty()) {
            throw std::runtime_error("incomplete prepared shader stage");
        }
        Warm::PreparedStore::Location info{}, code{};
        if (!infos[stage].empty()) {
            const auto compressed = Common::Compression::CompressDataZSTD(
                reinterpret_cast<const u8*>(spirv[stage].data()), spirv[stage].size() * sizeof(u32),
                3);
            const auto info_at = store.AddBlob(infos[stage]);
            const auto code_at = store.AddBlob(compressed);
            if (!info_at || !code_at) {
                throw std::runtime_error("cannot keep prepared shader");
            }
            info = *info_at;
            code = *code_at;
        }
        put(info.offset);
        put(info.size);
        put(code.offset);
        put(code.size);
    }
    return store.AddRecord(index, payload);
    };
}

bool PipelineCache::LoadPrecompiledInfos(const std::filesystem::path& precompiled,
                                         const std::filesystem::path& infos,
                                         const VideoCommon::ShaderWarm::PreparedStore& store) {
    namespace Warm = VideoCommon::ShaderWarm;
    precompiled_record_ids.clear();
    precompiled_stages.clear();
    precompiled_records.clear();
    precompiled_compute_records.clear();
    precompiled_info_blobs.clear();
    precompiled_file.close();
    precompiled_infos_file.close();
    u32 record_count{};
    try {
        std::ifstream prep(precompiled, std::ios::binary);
        prep.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        std::array<char, 8> magic{};
        std::array<u32, 4> header{};
        prep.read(magic.data(), magic.size());
        prep.read(reinterpret_cast<char*>(header.data()), sizeof(header));
        const u64 prep_size = std::filesystem::file_size(precompiled);
        if (std::string_view(magic.data(), magic.size()) != "NX2PREP1" ||
            prep_size < 24 || header[0] != CACHE_VERSION || header[3] != 0 ||
            header[2] > Warm::PreparedStore::MaxEntries || header[1] > Warm::PreparedStore::MaxEntries ||
            header[1] > (prep_size - 24) / 16) {
            return false;
        }
        record_count = header[2];
        precompiled_env_index.resize(header[1]);
        for (auto& [offset, size] : precompiled_env_index) {
            prep.read(reinterpret_cast<char*>(&offset), sizeof(offset));
            prep.read(reinterpret_cast<char*>(&size), sizeof(size));
            if (offset > prep_size || size > prep_size - offset ||
                size > Warm::PreparedStore::MaxPayloadBytes) {
                precompiled_env_index.clear();
                return false;
            }
        }
    } catch (const std::exception&) {
        precompiled_env_index.clear();
        return false;
    }
    std::ifstream file(infos, std::ios::binary);
    std::unordered_map<u64, u32> blob_ids;
    const auto blob_id = [&](const Warm::PreparedStore::Location& at) {
        const auto [it, added] =
            blob_ids.try_emplace(at.offset, static_cast<u32>(precompiled_info_blobs.size()));
        if (added) {
            precompiled_info_blobs.emplace_back(at.offset, at.size);
        }
        return it->second;
    };
    size_t rejected = 0;
    constexpr u32 Differs = ~0U - 1;
    std::map<std::tuple<u64, u8, u32>, u32> stage_infos;
    std::unordered_map<u32, bool> validated_stage_reuse;
    std::unordered_set<u32> validated_spirv;
    for (const auto& entry : store.Records()) {
        // A damaged record only loses that pipeline: its draws are translated as usual.
        const auto bytes = Warm::PreparedStore::ReadFrame(file, entry.at);
        size_t at = 0;
        const auto get = [&](auto& value) {
            if (sizeof(value) > bytes.size() - at) {
                return false;
            }
            std::memcpy(&value, bytes.data() + at, sizeof(value));
            at += sizeof(value);
            return true;
        };
        PrecompiledRecord record;
        u8 kind{}, count{};
        u16 pad{};
        bool valid = get(kind) && get(count) && get(pad) && get(record.envs) && kind <= 1 &&
                     count > 0 && count <= 5 && (kind == 0 || count == 1) && pad == 0 &&
                     entry.id < record_count;
        for (u8 i = 0; valid && i < count; ++i) {
            valid = record.envs[i] < precompiled_env_index.size();
        }
        u64 hash{};
        if (valid && kind == 1) {
            ComputePipelineCacheKey key;
            valid = get(key);
            hash = key.Hash();
            record.key.assign(reinterpret_cast<const u8*>(&key),
                              reinterpret_cast<const u8*>(&key) + sizeof(key));
        } else if (valid) {
            GraphicsPipelineCacheKey key;
            valid = get(key);
            hash = key.Hash();
            record.key.assign(reinterpret_cast<const u8*>(&key),
                              reinterpret_cast<const u8*>(&key) + sizeof(key));
        }
        record.env_count = count;
        record.infos.fill(NoInfo);
        record.spirv.fill(NoInfo);
        size_t stages = 0;
        for (size_t stage = 0; valid && stage < 5; ++stage) {
            Warm::PreparedStore::Location info{}, code{};
            valid = get(info.offset) && get(info.size) && get(code.offset) && get(code.size) &&
                    (info.size == 0) == (code.size == 0) &&
                    (info.size == 0 || (store.IsBlob(info) && store.IsBlob(code)));
            if (valid && info.size != 0) {
                record.infos[stage] = blob_id(info);
                record.spirv[stage] = blob_id(code);
                ++stages;
            }
        }
        if (!valid || stages != count || at != bytes.size()) {
            ++rejected;
            continue;
        }
        try {
            for (size_t stage = 0; stage < 5; ++stage) {
                if (record.infos[stage] == NoInfo) {
                    continue;
                }
                const u32 info_id = record.infos[stage];
                if (!validated_stage_reuse.contains(info_id)) {
                    const auto [offset, size] = precompiled_info_blobs.at(info_id);
                    const auto info_bytes = Warm::PreparedStore::ReadBlob(file, {offset, size});
                    std::span<const u8> input{info_bytes};
                    Shader::Info info;
                    if (!Shader::DeserializeInfo(input, info) || !input.empty()) {
                        throw std::runtime_error("invalid prepared info");
                    }
                    validated_stage_reuse.emplace(info_id, Warm::CanReusePreparedStageInfo(info));
                }
                if (!validated_spirv.contains(record.spirv[stage])) {
                    ReadKeptSpirv(file, precompiled_info_blobs.at(record.spirv[stage]));
                    validated_spirv.insert(record.spirv[stage]);
                }
            }
        } catch (const std::exception&) {
            ++rejected;
            continue;
        }
        precompiled_record_ids.insert(entry.id);
        if (kind == 0) {
            GraphicsPipelineCacheKey key;
            std::memcpy(&key, record.key.data(), sizeof(key));
            if (key.unique_hashes[0] == 0) {
                size_t position{};
                for (u8 stage = 0; stage < 5; ++stage) {
                    if (key.unique_hashes[stage + 1] == 0) {
                        continue;
                    }
                    if (position >= record.env_count) {
                        break;
                    }
                    const u32 env = record.envs[position++];
                    const u32 info = record.infos[stage];
                    if (info == NoInfo || !validated_stage_reuse.at(info)) {
                        continue;
                    }
                    const auto [it, added] = stage_infos.try_emplace(
                        std::tuple{key.unique_hashes[stage + 1], stage, env}, info);
                    if (!added && it->second != info) {
                        it->second = Differs;
                    }
                }
            }
        }
        if (kind == 1) {
            precompiled_compute_records.emplace(hash, std::move(record));
        } else {
            precompiled_records.emplace(hash, std::move(record));
        }
    }
    for (const auto& [stage_key, info] : stage_infos) {
        if (info != Differs) {
            const auto& [program, stage, env] = stage_key;
            precompiled_stages.emplace(program, PrecompiledStage{stage, env, info});
        }
    }
    precompiled_translation_policy = PreparedTranslationPolicy();
    precompiled_path = precompiled;
    precompiled_infos_path = infos;
    LOG_INFO(Render_Vulkan,
             "Prepared native shaders: {} graphics and {} compute pipelines, {} damaged records "
             "ignored",
             precompiled_records.size(), precompiled_compute_records.size(), rejected);
    return true;
}

/// A kept info blob, on the GPU thread; false when it is unreadable or needs layer emulation.
static bool ReadKeptInfo(std::ifstream& file, std::pair<u64, u32> blob, Shader::Info& info) {
    const auto bytes = VideoCommon::ShaderWarm::PreparedStore::ReadBlob(file, {blob.first, blob.second});
    std::span<const u8> input{bytes};
    info = {};
    return Shader::DeserializeInfo(input, info) && input.empty() && !info.requires_layer_emulation;
}

std::unique_ptr<GraphicsPipeline> PipelineCache::CreatePrecompiledGraphicsPipeline(
    GraphicsEnvironments& environments, std::optional<PrecompiledRecord>* matched) {
    if ((precompiled_records.empty() && precompiled_stages.empty()) || !PrecompiledFastPath() || precompiled_translation_policy != PreparedTranslationPolicy()) {
        return nullptr;
    }
    const u64 hash = graphics_key.Hash();
    // A vertex A program is merged into vertex B: not one of these.
    if (precompiled_rejected.contains(hash) || graphics_key.unique_hashes[0] != 0) {
        return nullptr;
    }
    // EDEN_NX2_PRECOMPILED_PER_STAGE=1: every pipeline is looked up per stage, to test it.
    static const bool per_stage_only = [] {
        const char* value = std::getenv("EDEN_NX2_PRECOMPILED_PER_STAGE");
        return value != nullptr && value[0] == '1';
    }();
    const auto it = precompiled_records.find(hash);
    if (it == precompiled_records.end() || per_stage_only) {
        std::string_view reason;
        auto pipeline = CreatePerStageGraphicsPipeline(environments, matched, reason);
        if (!pipeline && !reason.empty()) {
            CountPrecompiledReason(reason);
        }
        return pipeline;
    }
    const PrecompiledRecord& record = it->second;
    if (record.key.size() != sizeof(graphics_key) ||
        std::memcmp(record.key.data(), &graphics_key, sizeof(graphics_key)) != 0) {
        return nullptr;
    }
    const auto live = environments.Span();
    if (live.size() != record.env_count) {
        return nullptr;
    }
    auto recorded = std::make_shared<std::vector<FileEnvironment>>(record.env_count);
    std::array<Shader::Info, 5> infos{};
    std::array<bool, 5> present{};
    try {
        OpenPrecompiledFiles();
        for (size_t i = 0; i < record.env_count; ++i) {
            precompiled_file.clear();
            const auto [offset, size] = precompiled_env_index.at(record.envs[i]);
            precompiled_file.seekg(static_cast<std::streamoff>(offset));
            (*recorded)[i].Deserialize(precompiled_file,
                                       static_cast<std::streamoff>(offset + size));
        }
        for (size_t stage = 0; stage < infos.size(); ++stage) {
            if (record.infos[stage] == NoInfo) {
                continue;
            }
            if (!ReadKeptInfo(precompiled_infos_file,
                              precompiled_info_blobs.at(record.infos[stage]), infos[stage])) {
                CountPrecompiledReason("infos unreadable or with layer emulation");
                return nullptr;
            }
            present[stage] = true;
        }
    } catch (const std::exception&) {
        CountPrecompiledReason("files unreadable");
        precompiled_file.close();
        precompiled_infos_file.close();
        return nullptr;
    }
    // Each environment with the format traits its own stage's translation used.
    size_t position = 0;
    for (size_t program = 1; program < Maxwell::MaxShaderProgram; ++program) {
        if (graphics_key.unique_hashes[program] == 0) {
            continue;
        }
        if (position >= live.size()) {
            return nullptr;
        }
        const size_t stage = program - 1;
        const u32 traits = present[stage] ? UsedFormatTraits(infos[stage])
                                          : FileEnvironment::FormatTraitInteger;
        const char* why = "";
        if (!(*recorded)[position].AnswersHoldFor(*live[position], &why, traits)) {
            // The program may be kept in other environments, from other pipelines. The reason
            // counted is the predicted key's.
            std::string_view per_stage_reason;
            auto pipeline = CreatePerStageGraphicsPipeline(environments, matched, per_stage_reason);
            if (!pipeline) {
                CountPrecompiledReason(why);
            }
            return pipeline;
        }
        ++position;
    }
    // The worker builds the SPIR-V the preparation step kept: the translation of the recorded
    // environments by this build, which the check above found to give the game state's.
    // EDEN_NX2_PRECOMPILED_VERIFY=1 translates them again instead and compares the infos.
    static const bool verify = [] {
        const char* value = std::getenv("EDEN_NX2_PRECOMPILED_VERIFY");
        return value != nullptr && value[0] == '1';
    }();
    std::optional<std::array<std::pair<u64, u32>, 5>> spirv_blobs;
    if (!verify) {
        spirv_blobs.emplace();
        for (size_t stage = 0; stage < present.size(); ++stage) {
            if (!present[stage]) {
                continue;
            }
            if (record.spirv[stage] == NoInfo ||
                record.spirv[stage] >= precompiled_info_blobs.size()) {
                spirv_blobs.reset();
                break;
            }
            (*spirv_blobs)[stage] = precompiled_info_blobs[record.spirv[stage]];
        }
    }
    if (matched) {
        *matched = record;
    }
    return MakePrecompiledGraphicsPipeline(std::move(recorded), infos, present, spirv_blobs);
}

void PipelineCache::OpenPrecompiledFiles() {
    if (!precompiled_file.is_open()) {
        precompiled_file.open(precompiled_path, std::ios::binary);
        precompiled_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    }
    if (!precompiled_infos_file.is_open()) {
        precompiled_infos_file.open(precompiled_infos_path, std::ios::binary);
        precompiled_infos_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    }
}

std::unique_ptr<GraphicsPipeline> PipelineCache::CreatePerStageGraphicsPipeline(
    GraphicsEnvironments& environments, std::optional<PrecompiledRecord>* matched,
    std::string_view& reason) {
    if (precompiled_stages.empty() || VideoCommon::OotDlss::ObjectMotionRequested()) {
        return nullptr;
    }
    const auto live = environments.Span();
    auto recorded = std::make_shared<std::vector<FileEnvironment>>(live.size());
    std::array<Shader::Info, 5> infos{};
    std::array<bool, 5> present{};
    PrecompiledRecord found;
    found.envs.fill(NoInfo);
    found.infos.fill(NoInfo);
    found.spirv.fill(NoInfo);
    size_t position = 0;
    try {
        OpenPrecompiledFiles();
        for (size_t program = 1; program < Maxwell::MaxShaderProgram; ++program) {
            const u64 program_hash = graphics_key.unique_hashes[program];
            if (program_hash == 0) {
                continue;
            }
            if (position >= live.size()) {
                return nullptr;
            }
            const size_t stage = program - 1;
            // Each stage from the first of its program's recorded environments whose answers the
            // game's state gives.
            reason = "per stage: a program not kept";
            size_t tried = 0;
            const auto [first, last] = precompiled_stages.equal_range(program_hash);
            for (auto entry = first; entry != last && tried < MaxStageCandidates; ++entry) {
                const PrecompiledStage& candidate = entry->second;
                if (candidate.stage != stage || candidate.env >= precompiled_env_index.size()) {
                    continue;
                }
                ++tried;
                if (!ReadKeptInfo(precompiled_infos_file,
                                  precompiled_info_blobs[candidate.info], infos[stage]) ||
                    !VideoCommon::ShaderWarm::CanReusePreparedStageInfo(
                        infos[stage], VideoCommon::OotDlss::ObjectMotionRequested())) {
                    continue;
                }
                FileEnvironment env;
                const auto [offset, size] = precompiled_env_index[candidate.env];
                precompiled_file.clear();
                precompiled_file.seekg(static_cast<std::streamoff>(offset));
                env.Deserialize(precompiled_file, static_cast<std::streamoff>(offset + size));
                const char* why = "";
                if (!env.AnswersHoldFor(*live[position], &why, UsedFormatTraits(infos[stage]))) {
                    reason = "per stage: a program's answers differ";
                    continue;
                }
                (*recorded)[position] = std::move(env);
                found.envs[position] = candidate.env;
                found.infos[stage] = candidate.info;
                present[stage] = true;
                break;
            }
            if (!present[stage]) {
                return nullptr;
            }
            ++position;
        }
    } catch (const std::exception&) {
        reason = "files unreadable";
        precompiled_file.close();
        precompiled_infos_file.close();
        return nullptr;
    }
    reason = {};
    if (position != live.size()) {
        return nullptr;
    }
    size_t storage_buffers{};
    for (size_t stage = 0; stage < infos.size(); ++stage) {
        if (present[stage]) {
            storage_buffers += Shader::NumDescriptors(infos[stage].storage_buffers_descriptors);
        }
    }
    if (!FitsStorageBufferSetLimit(storage_buffers, 0, host_info.max_descriptor_set_storage_buffers)) {
        reason = "per stage: combined storage descriptors exceed the device limit";
        return nullptr;
    }
    found.env_count = static_cast<u8>(position);
    if (matched) {
        *matched = found;
    }
    precompiled_per_stage.fetch_add(1, std::memory_order_relaxed);
    return MakePrecompiledGraphicsPipeline(std::move(recorded), infos, present, std::nullopt);
}

std::unique_ptr<GraphicsPipeline> PipelineCache::MakePrecompiledGraphicsPipeline(
    std::shared_ptr<std::vector<FileEnvironment>> recorded,
    const std::array<Shader::Info, 5>& infos, const std::array<bool, 5>& present,
    const std::optional<std::array<std::pair<u64, u32>, 5>>& spirv_blobs) {
    VideoCommon::EvictedPointerSlots::Slots mapped_pointers;
    size_t mapped_env = 0;
    for (size_t program = 0; program < graphics_key.unique_hashes.size(); ++program) {
        if (graphics_key.unique_hashes[program] != 0) {
            mapped_pointers[program] = recorded->at(mapped_env++).MappedStoragePointerKeys();
        }
    }
    std::array<const Shader::Info*, 5> info_ptrs{};
    for (size_t stage = 0; stage < info_ptrs.size(); ++stage) {
        info_ptrs[stage] = present[stage] ? &infos[stage] : nullptr;
    }
    if (spirv_blobs) {
        auto build_kept = [this, key = graphics_key, path = precompiled_infos_path,
                           spirv_blobs = *spirv_blobs, present](
                              std::array<u128, Maxwell::MaxShaderStage>&) {
            std::array<vk::ShaderModule, Maxwell::MaxShaderStage> modules;
            for (size_t stage = 0; stage < modules.size(); ++stage) {
                if (present[stage]) {
                    modules[stage] = BuildKeptSpirv(device, path, spirv_blobs[stage], key.Hash());
                }
            }
            precompiled_from_spirv.fetch_add(1, std::memory_order_relaxed);
            return modules;
        };
        // The job reads and decompresses the kept SPIR-V, holding little.
        auto pipeline = std::make_unique<GraphicsPipeline>(
            scheduler, buffer_cache, texture_cache, vulkan_pipeline_cache, pipeline_binaries.get(),
            &shader_notify, device, descriptor_pool, guest_descriptor_queue, descriptor_buffer_ring,
            BuildQueue(true, 0, infos[0].is_task || infos[0].is_mesh), nullptr, render_pass_cache, graphics_key,
            std::array<vk::ShaderModule, Maxwell::MaxShaderStage>{}, info_ptrs,
            std::move(build_kept), std::nullopt);
        pipeline->SetMappedPointers(std::move(mapped_pointers));
        pipeline->MarkPrecompiled();
        return pipeline;
    }
    // The worker translates the recorded environments, which give the same programs as the
    // game's state does; the infos drawn with must be the ones that translation gives.
    auto emit_stages = [this, key = graphics_key, recorded, infos, present](
                           std::array<u128, Maxwell::MaxShaderStage>&) {
        ShaderPools pools;
        boost::container::static_vector<Shader::Environment*, Maxwell::MaxShaderProgram> env_ptrs;
        for (auto& env : *recorded) {
            env_ptrs.push_back(&env);
        }
        GraphicsStages stages;
        if (!TranslateGraphicsStages(pools, key, MakeSpan(env_ptrs), stages, false, nullptr,
                                     nullptr, false) ||
            stages.mesh_emulation) {
            throw Shader::LogicError("Precompiled pipeline {:016x} is not drawn natively",
                                     key.Hash());
        }
        std::array<vk::ShaderModule, Maxwell::MaxShaderStage> modules;
        Shader::Backend::Bindings binding;
        for (size_t stage = 0; stage < modules.size(); ++stage) {
            if (stages.runtime_infos[stage].has_value() != present[stage]) {
                throw Shader::LogicError("Precompiled pipeline {:016x}: other stages", key.Hash());
            }
            if (!present[stage]) {
                continue;
            }
            modules[stage] = EmitShaderModule(device, profile, *stages.runtime_infos[stage],
                                              stages.programs[stage + 1], binding,
                                              key.unique_hashes[stage + 1], stage);
            std::vector<u8> translated, stored;
            Shader::SerializeInfo(stages.programs[stage + 1].info, translated);
            Shader::SerializeInfo(infos[stage], stored);
            if (translated != stored) {
                throw Shader::LogicError(
                    "Precompiled pipeline {:016x}: stage {} infos differ from its translation",
                    key.Hash(), stage);
            }
        }
        precompiled_drawn.fetch_add(1, std::memory_order_relaxed);
        return modules;
    };
    auto pipeline = std::make_unique<GraphicsPipeline>(
        scheduler, buffer_cache, texture_cache, vulkan_pipeline_cache, pipeline_binaries.get(),
        &shader_notify, device, descriptor_pool, guest_descriptor_queue, descriptor_buffer_ring,
        BuildQueue(true, PrecompiledTranslationBytes, infos[0].is_task || infos[0].is_mesh),
        nullptr, render_pass_cache, graphics_key,
        std::array<vk::ShaderModule, Maxwell::MaxShaderStage>{}, info_ptrs,
        std::move(emit_stages), std::nullopt);
    pipeline->SetMappedPointers(std::move(mapped_pointers));
    pipeline->MarkPrecompiled();
    return pipeline;
}

ComputePipelineCacheEntry PipelineCache::CreatePrecompiledComputePipeline(
    const ComputePipelineCacheKey& key, ComputeEnvironment& env,
    const PrecompiledRecord** matched) {
    if (precompiled_compute_records.empty() || !PrecompiledFastPath() || precompiled_translation_policy != PreparedTranslationPolicy()) {
        return {};
    }
    const u64 hash = key.Hash();
    if (precompiled_rejected.contains(hash)) {
        return {};
    }
    // One record for each storage pointer state the program was prepared in.
    const auto [first, last] = precompiled_compute_records.equal_range(hash);
    const char* first_reason = nullptr;
    for (auto it = first; it != last; ++it) {
        const PrecompiledRecord& record = it->second;
        if (record.key.size() != sizeof(key) ||
            std::memcmp(record.key.data(), &key, sizeof(key)) != 0) {
            continue;
        }
        if (record.env_count != 1 || record.infos[0] == NoInfo || record.spirv[0] == NoInfo ||
            record.spirv[0] >= precompiled_info_blobs.size()) {
            continue;
        }
        FileEnvironment recorded;
        Shader::Info info{};
        try {
            OpenPrecompiledFiles();
            precompiled_file.clear();
            const auto [env_offset, env_size] = precompiled_env_index.at(record.envs[0]);
            precompiled_file.seekg(static_cast<std::streamoff>(env_offset));
            recorded.Deserialize(precompiled_file,
                                 static_cast<std::streamoff>(env_offset + env_size));
            if (!ReadKeptInfo(precompiled_infos_file, precompiled_info_blobs.at(record.infos[0]),
                              info)) {
                if (!first_reason) {
                    first_reason = "infos unreadable or with layer emulation";
                }
                continue;
            }
            if (info.uses_cooperative_matrix && !VideoCommon::EmulatesNativeTensorPrograms(
                    Settings::values.tensor_programs.GetValue(), Settings::GetCurrentProgramID(),
                    SupportsCooperativeHmma())) {
                continue;
            }
        } catch (const std::exception&) {
            CountPrecompiledReason("files unreadable");
            precompiled_file.close();
            precompiled_infos_file.close();
            return {};
        }
        u32 format_traits = FileEnvironment::FormatTraitInteger;
        if (std::ranges::any_of(info.image_descriptors,
                                [](const auto& desc) { return desc.is_byte_addressed; })) {
            format_traits |= FileEnvironment::FormatTraitByteAddressed;
        }
        if (!info.texture_buffer_descriptors.empty()) {
            format_traits |= FileEnvironment::FormatTraitSnorm;
        }
        const char* why = "";
        if (!recorded.AnswersHoldFor(env, &why, format_traits)) {
            if (!first_reason) {
                first_reason = why;
            }
            continue;
        }
        auto emit_module = [this, path = precompiled_infos_path,
                            blob = precompiled_info_blobs[record.spirv[0]], hash] {
            auto module = BuildKeptSpirv(device, path, blob, hash);
            precompiled_from_spirv.fetch_add(1, std::memory_order_relaxed);
            return module;
        };
        ComputePipelineCacheEntry entry{
            .pipeline = std::make_unique<ComputePipeline>(
                device, scheduler, vulkan_pipeline_cache, pipeline_binaries.get(), key.Hash(),
                descriptor_pool, guest_descriptor_queue, descriptor_buffer_ring, BuildQueue(true),
                nullptr, &shader_notify, info, vk::ShaderModule{}, key.unique_hash,
                std::move(emit_module)),
            // The live environment answered the same questions, as the translation would have.
            .null_pointer_slots = env.IsNativeProgram() ? env.NullStoragePointerSlots()
                                                        : std::vector<std::pair<u32, u32>>{},
            .mapped_pointers =
                env.IsNativeProgram() ? env.MappedStoragePointerKeys() : std::vector<u64>{}};
        entry.pipeline->MarkPrecompiled();
        if (matched) {
            *matched = &record;
        }
        return entry;
    }
    if (first_reason) {
        CountPrecompiledReason(first_reason);
    }
    return {};
}

void PipelineCache::AuditPrecompiled(std::span<Shader::Environment* const> envs, bool compute,
                                     u64 key_hash, const PrecompiledRecord* given) {
    const auto it = precompiled_records.find(key_hash);
    if (!given && (compute || it == precompiled_records.end())) {
        return;
    }
    const PrecompiledRecord& record = given ? *given : it->second;
    const auto differ = [&](size_t stage, const char* what) {
        if (audit_differ.fetch_add(1, std::memory_order_relaxed) < 32) {
            LOG_ERROR(Render_Vulkan, "Precompiled audit: {} pipeline {:016x} stage {}: {} differ",
                      compute ? "compute" : "graphics", key_hash, stage, what);
        }
    };
    try {
        std::array<std::vector<u32>, Maxwell::MaxShaderStage> live_code;
        std::array<std::vector<u8>, Maxwell::MaxShaderStage> live_info;
        std::array<bool, Maxwell::MaxShaderStage> present{};
        main_pools.ReleaseContents();
        if (compute) {
            Shader::Maxwell::Flow::CFG cfg{*envs[0], main_pools.flow_block,
                                           envs[0]->StartAddress()};
            auto program{TranslateProgram(main_pools.inst, main_pools.block, *envs[0], cfg,
                                          host_info)};
            live_code[0] = ComputeModuleCode(EmitSPIRV(profile, program), key_hash);
            Shader::SerializeInfo(program.info, live_info[0]);
            present[0] = true;
        } else {
            GraphicsStages stages;
            if (!TranslateGraphicsStages(main_pools, graphics_key, envs, stages, false, nullptr,
                                         nullptr, false)) {
                differ(0, "the pipeline (skipped by its translation)");
                return;
            }
            Shader::Backend::Bindings binding;
            for (size_t stage = 0; stage < live_code.size(); ++stage) {
                if (!stages.runtime_infos[stage]) {
                    continue;
                }
                live_code[stage] =
                    EmitPipelineShader(profile, *stages.runtime_infos[stage],
                                       stages.programs[stage + 1], binding,
                                       graphics_key.unique_hashes[stage + 1], {});
                if (GraphicsNoInline()) {
                    VideoCommon::MarkSharedFunctionsDontInline(live_code[stage]);
                }
                Shader::SerializeInfo(stages.programs[stage + 1].info, live_info[stage]);
                present[stage] = true;
            }
        }
        bool same = true;
        for (size_t stage = 0; stage < present.size(); ++stage) {
            if (present[stage] != (record.infos[stage] != NoInfo)) {
                differ(stage, "stages");
                same = false;
                continue;
            }
            if (!present[stage]) {
                continue;
            }
            const auto [offset, size] = precompiled_info_blobs.at(record.infos[stage]);
            const auto kept_info = VideoCommon::ShaderWarm::PreparedStore::ReadBlob(
                precompiled_infos_file, {offset, size});
            if (kept_info != live_info[stage]) {
                differ(stage, "infos");
                same = false;
            }
            if (record.spirv[stage] != NoInfo &&
                ReadKeptSpirv(precompiled_infos_file,
                              precompiled_info_blobs.at(record.spirv[stage])) != live_code[stage]) {
                differ(stage, "SPIR-V words");
                same = false;
            }
        }
        if (same) {
            audit_same.fetch_add(1, std::memory_order_relaxed);
        }
    } catch (const std::exception& exception) {
        differ(0, exception.what());
    }
}

GraphicsPipeline* PipelineCache::ReplaceFailedPrecompiled() {
    precompiled_rejected.insert(graphics_key.Hash());
    precompiled_failed.fetch_add(1, std::memory_order_relaxed);
    if (const auto it = graphics_cache.find(graphics_key); it != graphics_cache.end()) {
        used_this_session.erase(it->second.get());
        evicted_pipelines.emplace_back(scheduler.CurrentTick(), std::move(it->second));
        graphics_cache.erase(it);
    }
    for (auto& [key, pipeline] : graphics_cache) {
        if (pipeline) {
            pipeline->ClearTransitions();
        }
    }
    current_pipeline = nullptr;
    return CurrentGraphicsPipelineSlowPath();
}

void PipelineCache::QueueVulkanPipelineCacheFlush() {
    if (!use_vulkan_pipeline_cache || vulkan_pipeline_cache_filename.empty() ||
        !device.CanReadPipelineCacheDuringBuilds()) {
        return;
    }
    if (++pipelines_since_flush < VULKAN_CACHE_FLUSH_PIPELINES) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto megabytes = last_cache_size.load(std::memory_order_relaxed) / (1024 * 1024);
    const std::chrono::seconds interval{
        std::max<size_t>(VULKAN_CACHE_FLUSH_MIN_SECONDS, megabytes)};
    if (last_flush.time_since_epoch().count() != 0 && now - last_flush < interval) {
        return;
    }
    if (flush_in_flight.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    pipelines_since_flush = 0;
    last_flush = now;
    serialization_thread.QueueWork([this] {
        SerializeVulkanPipelineCache(vulkan_pipeline_cache_filename, vulkan_pipeline_cache,
                                     CACHE_VERSION);
        size_t size = 0;
        vulkan_pipeline_cache.Read(&size, nullptr);
        last_cache_size.store(size, std::memory_order_relaxed);
        flush_in_flight.store(false, std::memory_order_release);
    });
}

void PipelineCache::QueueVulkanPipelineCacheSave() {
    if (!use_vulkan_pipeline_cache || vulkan_pipeline_cache_filename.empty()) {
        return;
    }
    // Not skipped when a timed flush is in flight: that one may have read the cache before the
    // stage's last builds. The serialization thread runs the two one after the other.
    serialization_thread.QueueWork([this] {
        SerializeVulkanPipelineCache(vulkan_pipeline_cache_filename, vulkan_pipeline_cache,
                                     CACHE_VERSION);
        size_t size = 0;
        vulkan_pipeline_cache.Read(&size, nullptr);
        last_cache_size.store(size, std::memory_order_relaxed);
        LOG_INFO(Render_Vulkan, "Driver pipeline cache saved after the shader precache, {} bytes",
                 size);
    });
}

namespace {
// EDEN_NX2_PIPELINE_DIFF=1: for a new graphics pipeline whose shaders an earlier one uses too,
// log which parts of the fixed state differ from that one (finds state that splits the same
// draws into many pipelines, each translated on the GPU thread).
void ReportNewPipelineKey(const GraphicsPipelineCacheKey& key) {
    static const bool enabled = std::getenv("EDEN_NX2_PIPELINE_DIFF") != nullptr;
    if (!enabled) {
        return;
    }
    static std::mutex mutex;
    std::scoped_lock lock{mutex};
    static std::unordered_map<u64, GraphicsPipelineCacheKey> last_by_shaders;
    static u32 logged{};
    const u64 shaders = Common::CityHash64(reinterpret_cast<const char*>(key.unique_hashes.data()),
                                           sizeof(key.unique_hashes));
    const auto [it, first] = last_by_shaders.try_emplace(shaders, key);
    if (first) {
        return;
    }
    const FixedPipelineState& now = key.state;
    const FixedPipelineState& before = it->second.state;
    std::string diffs;
    if (now.raw1 != before.raw1) diffs += fmt::format(" raw1^{:#x}", now.raw1 ^ before.raw1);
    if (now.raw2 != before.raw2) diffs += fmt::format(" raw2^{:#x}", now.raw2 ^ before.raw2);
    if (now.color_formats != before.color_formats) {
        diffs += fmt::format(" color_formats {:02x} -> {:02x}", fmt::join(before.color_formats, ""),
                             fmt::join(now.color_formats, ""));
    }
    if (now.alpha_test_ref != before.alpha_test_ref) {
        diffs += fmt::format(" alpha_test_ref {:#x} -> {:#x} (func {})", before.alpha_test_ref,
                             now.alpha_test_ref, now.alpha_test_func.Value());
    }
    if (now.point_size != before.point_size) {
        diffs += fmt::format(" point_size {:#x} -> {:#x}", before.point_size, now.point_size);
    }
    if (now.viewport_swizzles != before.viewport_swizzles) diffs += " viewport_swizzles";
    if (now.attribute_types != before.attribute_types) {
        diffs += fmt::format(" attribute_types^{:#x}", now.attribute_types ^ before.attribute_types);
    }
    if (std::memcmp(&now.dynamic_state, &before.dynamic_state, sizeof(now.dynamic_state)) != 0) {
        diffs += " dynamic_state";
    }
    if (now.driver_id != before.driver_id || now.driver_version != before.driver_version) {
        diffs += " driver";
    }
    const size_t size = now.Size();
    if (diffs.empty() && size > offsetof(FixedPipelineState, attachments)) {
        diffs = fmt::format(" past attachments (key size {})", size);
    }
    it->second = key;
    if (logged++ < 400) {
        LOG_INFO(Render_Vulkan, "New pipeline for known shaders {:016x} differs in:{}", shaders,
                 diffs.empty() ? std::string{" nothing in the key prefix"} : diffs);
    }
}

// Without VK_EXT_mesh_shader: logs, once per shader set, what a skipped task/mesh pipeline would
// need from an emulation (task stage, per-primitive outputs, topology, output sizes).
template <typename Programs>
void ReportUnsupportedMeshPipeline(const GraphicsPipelineCacheKey& key, const Programs& programs) {
    static std::mutex mutex;
    static std::unordered_set<u64> seen;
    static u32 with_task{}, per_primitive{}, non_triangle{}, total{};
    u64 shaders{};
    for (const u64 hash : key.unique_hashes) {
        shaders = shaders * 31 + hash;
    }
    std::scoped_lock lock{mutex};
    if (!seen.insert(shaders).second) {
        return;
    }
    const Shader::IR::Program* task{};
    const Shader::IR::Program* mesh{};
    bool has_pixel{};
    for (size_t index = 0; index < programs.size(); ++index) {
        if (key.unique_hashes[index] == 0) {
            continue;
        }
        const auto& program{programs[index]};
        if (program.stage == Shader::Stage::Task) {
            task = &program;
        } else if (program.stage == Shader::Stage::Mesh) {
            mesh = &program;
        } else if (program.stage == Shader::Stage::Fragment) {
            has_pixel = true;
        }
    }
    ++total;
    if (mesh == nullptr) {
        LOG_WARNING(Render_Vulkan, "Mesh emulation survey: pipeline {:016x} without a mesh stage",
                    shaders);
        return;
    }
    const auto& info{mesh->mesh};
    u32 position{}, generic{}, other{};
    std::string others;
    for (const u32 line : info.vertex_lines) {
        const auto attribute{static_cast<Shader::IR::Attribute>(line)};
        if (attribute >= Shader::IR::Attribute::PositionX &&
            attribute <= Shader::IR::Attribute::PositionW) {
            ++position;
        } else if (Shader::IR::IsGeneric(attribute)) {
            ++generic;
        } else {
            ++other;
            others += fmt::format(" {}", line);
        }
    }
    with_task += task != nullptr;
    per_primitive += !info.primitive_lines.empty();
    non_triangle += info.topology != 4;
    LOG_WARNING(Render_Vulkan,
                "Mesh emulation survey #{}: task={} (payload {} words, wg {}x{}x{}) mesh wg "
                "{}x{}x{} max_vertices={} max_primitives={} topology={} vertex_lines={} "
                "(position {} generic {} other {}:{}) primitive_lines={} pixel={} | totals: "
                "task {} per-primitive {} non-triangle {}",
                total, task != nullptr, info.payload_words, task ? task->workgroup_size[0] : 0,
                task ? task->workgroup_size[1] : 0, task ? task->workgroup_size[2] : 0,
                mesh->workgroup_size[0], mesh->workgroup_size[1], mesh->workgroup_size[2],
                info.max_vertices, info.max_primitives, info.topology, info.vertex_lines.size(),
                position, generic, other, others, info.primitive_lines.size(), has_pixel,
                with_task, per_primitive, non_triangle);
}
} // Anonymous namespace

GraphicsPipeline* PipelineCache::CurrentGraphicsPipelineSlowPath() {
    namespace StutterTrace = VideoCommon::StutterTrace;
    const auto [pair, is_new]{graphics_cache.try_emplace(graphics_key)};
    auto& pipeline{pair->second};
    if (is_new) {
        ReportNewPipelineKey(graphics_key);
        if (scheduled_prebuild) {
            pipeline = scheduled_prebuild->Take(graphics_key);
        }
        if (StutterTrace::Enabled()) {
            StutterTrace::CountLookup(pipeline ? StutterTrace::Lookup::GraphicsBackground
                                      : disk_pipelines.contains(graphics_key.Hash())
                                          ? StutterTrace::Lookup::GraphicsRecorded
                                          : StutterTrace::Lookup::GraphicsNew);
        }
        if (!pipeline) {
            const auto keep = evicted_pointer_slots.Take(graphics_key.Hash());
            pipeline = CreateGraphicsPipeline(keep ? &*keep : nullptr);
        }
    } else if (StutterTrace::Enabled() && pipeline && !pipeline->WasUsed()) {
        StutterTrace::CountLookup(StutterTrace::Lookup::GraphicsPrebuilt);
    }
    if (!pipeline) {
        return nullptr;
    }
    constexpr auto save_interval = std::chrono::seconds{15};
    if (used_this_session.insert(pipeline.get()).second &&
        std::chrono::steady_clock::now() - used_pipelines_saved > save_interval) {
        SaveUsedPipelines();
    }
    if (current_pipeline) {
        current_pipeline->AddTransition(pipeline.get());
    }
    current_pipeline = pipeline.get();
    if (current_pipeline->IsPrecompiled() && current_pipeline->Failed()) {
        return ReplaceFailedPrecompiled();
    }
    current_pipeline->MarkUsed(frame_count);
    return BuiltPipeline(current_pipeline);
}

PipelineCache::WarmResult PipelineCache::WarmGraphicsLibraries(
    ShaderPools& pools, const GraphicsPipelineCacheKey& key,
    std::span<Shader::Environment* const> envs) {
    if (!device.IsExtGraphicsPipelineLibrarySupported()) {
        return WarmResult::Unbuildable;
    }
    pools.ReleaseContents();
    const auto pipeline{
        CreateGraphicsPipeline(pools, key, envs, nullptr, false, nullptr, nullptr, true)};
    if (!pipeline) {
        return WarmResult::Unbuildable;
    }
    return pipeline->Failed() ? WarmResult::DriverFailed : WarmResult::Built;
}

/// Per-frame upkeep: reports library counters, frees evicted and retired pipelines the GPU is done
/// with, and every 600 frames evicts unused pipelines and forgets old render target records.
void PipelineCache::TickFrame() {
    ++frame_count;
    native_wait_budget_us =
        static_cast<s64>(Settings::values.native_pipeline_wait_ms.GetValue()) * 1000;
    if (device.IsExtGraphicsPipelineLibrarySupported()) {
        graphics_libraries.Report();
    }
    while (!evicted_pipelines.empty() && scheduler.IsFree(evicted_pipelines.front().first)) {
        evicted_pipelines.pop_front();
    }
    while (!retired_compute_pipelines.empty() &&
           scheduler.IsFree(retired_compute_pipelines.front().first) &&
           retired_compute_pipelines.front().second->IsBuildTaskDone()) {
        retired_compute_pipelines.pop_front();
    }
    constexpr u64 EvictCheckFrames = 600;
    if (frame_count % EvictCheckFrames == 0) {
        EvictUnusedPipelines();
        std::erase_if(target_frames, [this](const auto& entry) {
            return entry.second.last + EvictCheckFrames < frame_count;
        });
        std::erase_if(cleared_frames, [this](const auto& entry) {
            return entry.second + EvictCheckFrames < frame_count;
        });
    }
}

/// Desktop retention follows its RAM bound; the frame-based fallback scales unused prebuilds
/// with device memory. Both wait until the GPU has finished with a pipeline.
void PipelineCache::EvictUnusedPipelines() {
    // Building a pipeline again on MoltenVK means a Metal compile mid-game, far dearer than the
    // rebuild cost below, so none are evicted there.
    if (device.IsMoltenVK()) {
        return;
    }
    // EDEN_NX2_PRELOAD_ALL_PIPELINES asked for every cached pipeline built: evicting the ones not
    // used yet threw the preload away after 9000 frames (Ocarina: 22,510 of 24,285, then rebuilt
    // one by one in new places).
    if (PreloadAllPipelines(device.IsMoltenVK())) {
        return;
    }
    // Each built pipeline holds ~0.45 MB of driver memory and none was ever destroyed, so it grew
    // with every place a session went (title 1: 1,500 on its title screen alone). One unused for
    // EvictAfterFrames may be destroyed. Rebuilding it later requires a full GPU-thread shader
    // translation (~8.5 ms each) plus a driver pipeline cache hit, and pausing or a long cutscene
    // evicts an area's pipelines so returning rebuilds them all at once (OoT: 8,158 rebuilt in
    // 80 min, multi-second freezes). So on desktop they are only destroyed past kept_pipelines
    // built, least recently used first.
    if (frame_count <= EvictAfterFrames) {
        return;
    }
    const u64 cutoff = frame_count - EvictAfterFrames;
    const u64 tick = scheduler.CurrentTick();
    size_t evicted = 0;
    const auto evict = [&](auto it) {
        if (used_this_session.erase(it->second.get()) != 0) {
            used_and_evicted.insert(it->first.Hash());
        }
        evicted_pointer_slots.Remember(it->first.Hash(), it->second->MappedPointers());
        evicted_pipelines.emplace_back(tick, std::move(it->second));
        ++evicted;
        return graphics_cache.erase(it);
    };
    const auto evictable = [&](const GraphicsPipeline* pipeline) {
        return pipeline != nullptr && pipeline != current_pipeline && pipeline->IsBuilt() &&
               pipeline->LastUse() < cutoff;
    };
    if (const size_t kept = kept_pipelines.load(std::memory_order::relaxed); kept != 0) {
        const size_t built = static_cast<size_t>(
            std::count_if(graphics_cache.begin(), graphics_cache.end(), [](const auto& entry) {
                return entry.second != nullptr && entry.second->IsBuilt();
            }));
        if (built <= kept) {
            return;
        }
        std::vector<GraphicsPipelineCacheKey> keys;
        std::vector<u64> last_uses;
        for (const auto& [key, pipeline] : graphics_cache) {
            if (evictable(pipeline.get())) {
                keys.push_back(key);
                // A prebuilt pipeline no draw has used counts as used at boot.
                last_uses.push_back(pipeline->WasUsed() ? pipeline->LastUse() : 0);
            }
        }
        for (const size_t index : PipelinesToEvict(last_uses, built, kept)) {
            if (const auto it = graphics_cache.find(keys[index]); it != graphics_cache.end()) {
                evict(it);
            }
        }
    } else {
        const size_t MaxUnusedPrebuiltPipelines = std::clamp<size_t>(
            device.GetDeviceLocalMemory() / (size_t{3} << 20), 2000, 10000);
        size_t kept_prebuilt = 0;
        for (auto it = graphics_cache.begin(); it != graphics_cache.end();) {
            GraphicsPipeline* const pipeline = it->second.get();
            if (!evictable(pipeline)) {
                ++it;
                continue;
            }
            // A pipeline built from the disk cache at boot that no draw has used yet stays, up to
            // a budget: evicting it throws the prebuild away, and the game translates it again on
            // the GPU thread when it reaches that content (OoT: 1174 of 1346 evicted on the title
            // screen, 684 rebuilt in 10 s on entering the field). Past the budget they go like the
            // rest, so a large cache can't hold their driver memory for the whole session.
            if (!pipeline->WasUsed() && kept_prebuilt < MaxUnusedPrebuiltPipelines) {
                ++kept_prebuilt;
                ++it;
                continue;
            }
            it = evict(it);
        }
    }
    if (evicted == 0) {
        return;
    }
    for (auto& [key, pipeline] : graphics_cache) {
        if (pipeline) {
            pipeline->ClearTransitions();
        }
    }
    LOG_INFO(Render_Vulkan, "Evicted {} graphics pipelines unused for {} frames, {} left", evicted,
             EvictAfterFrames, graphics_cache.size());
}

void PipelineCache::LogEvictionPolicy() const {
    if (device.IsMoltenVK()) {
        LOG_INFO(Render_Vulkan, "Pipeline eviction: off on MoltenVK");
    } else if (PreloadAllPipelines(device.IsMoltenVK())) {
        LOG_INFO(Render_Vulkan, "Pipeline eviction: off (EDEN_NX2_PRELOAD_ALL_PIPELINES=1)");
    } else if (const size_t kept = kept_pipelines.load(std::memory_order::relaxed); kept != 0) {
        LOG_INFO(Render_Vulkan,
                 "Pipeline eviction: unused pipelines are destroyed only past {} built", kept);
    } else {
        LOG_INFO(Render_Vulkan,
                 "Pipeline eviction: pipelines unused for {} frames are destroyed; up to {} "
                 "unused prebuilt pipelines are retained",
                 EvictAfterFrames,
                 std::clamp<size_t>(device.GetDeviceLocalMemory() / (size_t{3} << 20), 2000, 10000));
    }
}
void PipelineCache::NoteClear(u64 color_address, u64 depth_address) {
    if (HeavySkipWords() == 0) {
        return;
    }
    if (color_address != 0) {
        cleared_frames[color_address] = frame_count;
    }
    if (depth_address != 0) {
        cleared_frames[depth_address] = frame_count;
    }
    // The next lookup decides again with this clear known.
    last_target_key = ~0ULL;
}

bool PipelineCache::CurrentTargetIsRedrawn() {
    const auto& regs = maxwell3d->regs;
    const u64 color = regs.rt_control.count > 0 && regs.rt[0].format != Tegra::RenderTargetFormat::NONE
                          ? regs.rt[0].Address()
                          : 0;
    const u64 depth = regs.zeta_enable != 0 ? regs.zeta.Address() : 0;
    if (color == 0 && depth == 0) {
        last_target_reason = TargetReason::NewTarget;
        return false;
    }
    const u64 target = color * 31 + depth;
    // Most draws repeat the last target; the map is touched once per target change.
    if (target != last_target_key || frame_count != last_target_frame) {
        last_target_key = target;
        last_target_frame = frame_count;
        auto [it, inserted] = target_frames.try_emplace(target, TargetFrames{frame_count, 0});
        if (!inserted && it->second.last != frame_count) {
            it->second.previous = it->second.last;
            it->second.last = frame_count;
        }
        // Drawn in one of the two frames before and cleared earlier in this one: the target is
        // rebuilt every frame. Without the clear it may be a cache the game only patches (tiles,
        // trails), where a skipped draw would stay.
        const auto cleared_now = [this](u64 address) {
            if (address == 0) {
                return false;
            }
            const auto found = cleared_frames.find(address);
            return found != cleared_frames.end() && found->second == frame_count;
        };
        // A wide target of at least 1280 pixels is the scene itself (G-buffer, main image),
        // which is redrawn every frame even in the first frames of a new place; baked textures
        // (cube maps, shadow maps, atlases) are square or small.
        const u32 width = color != 0 ? regs.rt[0].width : regs.zeta_size.width;
        const u32 height = color != 0 ? regs.rt[0].height : regs.zeta_size.height;
        const bool scene_sized = width >= 1280 && width > height;
        const bool recent = it->second.previous != 0 && it->second.previous + 2 >= frame_count;
        // Every attachment the draw has must have been cleared: a depth-only clear says
        // nothing about color target 0, and the other way round.
        const bool all_cleared = (color != 0 || depth != 0) &&
                                 (color == 0 || cleared_now(color)) &&
                                 (depth == 0 || cleared_now(depth));
        if (!all_cleared) {
            last_target_reason = TargetReason::NotCleared;
        } else if (!recent && !scene_sized) {
            last_target_reason = TargetReason::NewTarget;
        } else {
            last_target_reason = TargetReason::Redrawn;
        }
        last_target_redrawn = last_target_reason == TargetReason::Redrawn;
    }
    return last_target_redrawn;
}

/// Returns `pipeline` when it is built or the current draw must wait for it, and nullptr when the
/// draw may be skipped while it builds (asynchronous shaders; native draws only with
/// EDEN_NX2_ASYNC_DISCARD=1; heavy pipelines with EDEN_NX2_HEAVY_SKIP). Each nullptr returned for
/// an unbuilt pipeline is counted for the log.
GraphicsPipeline* PipelineCache::BuiltPipeline(GraphicsPipeline* pipeline) {
    static const size_t heavy_skip_words = HeavySkipWords();
    // Tracked on every draw so a target's history is known before a heavy pipeline needs it.
    const bool redrawn = heavy_skip_words != 0 && CurrentTargetIsRedrawn();
    if (pipeline->IsBuilt()) {
        if (pipeline->heavy_skipped_draws != 0) {
            LOG_INFO(Render_Vulkan,
                     "Nx2Perf heavy_skip: {:016x} ({} instructions) built, {} draws skipped over "
                     "{:.0f} ms",
                     pipeline->KeyHash(), pipeline->SourceWords(), pipeline->heavy_skipped_draws,
                     std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - pipeline->heavy_skip_begin)
                         .count());
            pipeline->heavy_skipped_draws = 0;
        }
        return pipeline;
    }
    if (!use_asynchronous_shaders) {
        return pipeline;
    }
    // EDEN_NX2_ASYNC_DISCARD=1: skip native draws whose pipeline is still building instead of
    // waiting for it (experimental; the draw may be missing for a frame or more).
    /// Reads EDEN_NX2_ASYNC_DISCARD once and logs when it is on.
    static const bool discard_native = [] {
        const char* value = std::getenv("EDEN_NX2_ASYNC_DISCARD");
        const bool enabled = value != nullptr && value[0] == '1';
        if (enabled) {
            LOG_INFO(Render_Vulkan, "EDEN_NX2_ASYNC_DISCARD: native draws with a depth target skip "
                                    "unbuilt pipelines, others once many builds are pending");
        }
        return enabled;
    }();
    // A skipped draw that runs once (text rendered to a texture) is never drawn again: without a
    // depth target, only skip while so many pipelines are pending that waiting would stall for
    // seconds. Scene draws into a depth target come back on the next frame.
    static const u32 discard_min_pending = [] {
        constexpr u32 default_min_pending = 32;
        const char* value = std::getenv("EDEN_NX2_ASYNC_DISCARD_MIN_PENDING");
        if (value == nullptr) {
            return default_min_pending;
        }
        const char* const end = value + std::strlen(value);
        u32 parsed{};
        const auto result = std::from_chars(value, end, parsed);
        return result.ec == std::errc{} && result.ptr == end ? parsed : default_min_pending;
    }();
    const auto& draw_state = maxwell3d->draw_manager.draw_state;
    const bool native = maxwell3d->GetNativeMacroEngine().native_turing;
    pipeline->hold_reason = !native || heavy_skip_words == 0 ? 0 : 1;
    {
        const auto& regs = maxwell3d->regs;
        const bool color = regs.rt_control.count > 0 &&
                           regs.rt[0].format != Tegra::RenderTargetFormat::NONE;
        pipeline->hold_target_color = color;
        pipeline->hold_target_width = color ? regs.rt[0].width : regs.zeta_size.width;
        pipeline->hold_target_height = color ? regs.rt[0].height : regs.zeta_size.height;
    }
    if (native && heavy_skip_words != 0 && pipeline->SourceWords() >= heavy_skip_words) {
        // A heavy pipeline takes the driver 0.5-5 s to build; its draws are skipped meanwhile
        // unless skipping would lose data for good.
        if (pipeline->WritesStorage() || pipeline->UsesTransformFeedback()) {
            pipeline->hold_reason = 2;
            ++heavy_skip_counters.held_storage;
            return pipeline;
        }
        if (!redrawn) {
            if (last_target_reason == TargetReason::NotCleared) {
                pipeline->hold_reason = 3;
                ++heavy_skip_counters.held_not_cleared;
            } else {
                pipeline->hold_reason = 4;
                ++heavy_skip_counters.held_new_target;
            }
            return pipeline;
        }
        if (pipeline->heavy_skipped_draws++ == 0) {
            pipeline->heavy_skip_begin = std::chrono::steady_clock::now();
            ++heavy_skip_counters.pipelines;
        }
        ++heavy_skip_counters.draws;
        return nullptr;
    }
    // The environment variable wins over the setting; the setting is read per call.
    NativePendingMode mode = NativePendingMode::Wait;
    if (native) {
        switch (Settings::values.native_async_draws.GetValue()) {
        case Settings::NativeAsyncDraws::Bounded:
            mode = NativePendingMode::Bounded;
            break;
        case Settings::NativeAsyncDraws::Skip:
            mode = NativePendingMode::Skip;
            break;
        default:
            break;
        }
        if (discard_native) {
            const auto pending_jobs = [this] {
                const auto priorities = shader_jobs->Stats().priorities;
                const auto& need_now =
                    priorities[static_cast<size_t>(VideoCommon::ShaderJobPriority::NeedNow)];
                const auto& soon =
                    priorities[static_cast<size_t>(VideoCommon::ShaderJobPriority::Soon)];
                return need_now.queued + need_now.running + soon.queued + soon.running;
            };
            mode = maxwell3d->regs.zeta_enable != 0 || pending_jobs() >= discard_min_pending
                       ? NativePendingMode::Skip
                       : NativePendingMode::Wait;
        }
    }
    // Native draws are judged by the heuristic only when a mode other than Wait is chosen.
    const bool can_discard = CanDiscardPendingDraw(
        native, maxwell3d->regs.zeta_enable != 0, draw_state.draw_element_count,
        draw_state.draw_element_count, native && mode != NativePendingMode::Wait);
    bool discard = can_discard;
    if (native) {
        switch (DecideNativePending(mode, can_discard, native_wait_budget_us)) {
        case NativeDrawAction::Wait:
            discard = false;
            break;
        case NativeDrawAction::Skip:
            discard = true;
            break;
        case NativeDrawAction::WaitTimed: {
            const auto begin = std::chrono::steady_clock::now();
            const bool built =
                pipeline->WaitBuilt(std::chrono::microseconds{native_wait_budget_us});
            const auto spent = std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now() - begin)
                                   .count();
            native_wait_budget_us -= spent;
            native_wait_used_us += spent;
            static auto window = std::chrono::steady_clock::now();
            if (std::chrono::steady_clock::now() - window > std::chrono::seconds{10}) {
                LOG_INFO(Render_Vulkan,
                         "Bounded pipeline wait: {:.1f} ms spent in the last 10 s (budget {} ms "
                         "per frame)",
                         static_cast<double>(native_wait_used_us) / 1000.0,
                         Settings::values.native_pipeline_wait_ms.GetValue());
                native_wait_used_us = 0;
                window = std::chrono::steady_clock::now();
            }
            if (built) {
                return pipeline;
            }
            discard = true;
            break;
        }
        }
    }
    if (discard) {
        static std::atomic<u64> skipped{};
        if ((skipped.fetch_add(1, std::memory_order_relaxed) & 255) == 0) {
            // Counts lookups, not draws: a draw state can look its pipeline up more than once.
            LOG_INFO(Render_Vulkan,
                     "Async shaders: {} pipeline lookups skipped for unbuilt pipelines",
                     skipped.load(std::memory_order_relaxed));
        }
    }
    return discard ? nullptr : pipeline;
}

bool PipelineCache::TranslateGraphicsStages(ShaderPools& pools, const GraphicsPipelineCacheKey& key,
                                            std::span<Shader::Environment* const> envs,
                                            GraphicsStages& out, bool reference_spirv,
                                            ParallelTranslation* parallel,
                                            std::vector<std::shared_ptr<ShaderPools>>* stage_pools,
                                            bool allow_libraries) {
    auto hash = key.Hash();
    std::vector<VideoCommon::StageTranslation> translations;
    std::array<VideoCommon::StageTranslation*, Maxwell::MaxShaderProgram> translated{};
    if (parallel && stage_pools) {
        translations.reserve(envs.size());
        size_t stage_env{};
        for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {
            if (key.unique_hashes[index] == 0) {
                continue;
            }
            Shader::Environment& env{*envs[stage_env++]};
            if (index == static_cast<size_t>(Maxwell::ShaderType::Geometry) &&
                !device.IsGeometryShaderSupported() && env.IsNativeProgram()) {
                continue; // The pipeline is skipped below before this stage is needed
            }
            translations.push_back({
                .env = &env,
                .pools = stage_pools->emplace_back(TakeSparePools()).get(),
                .cfg_offset = static_cast<u32>(env.StartAddress() + sizeof(Shader::ProgramHeader)),
                .exits_to_dispatcher = index == 0,
            });
            translated[index] = &translations.back();
        }
        const auto begin = std::chrono::steady_clock::now();
        VideoCommon::TranslateStages(*shader_jobs, translations, host_info);
        parallel->wait += std::chrono::steady_clock::now() - begin;
        for (const VideoCommon::StageTranslation& translation : translations) {
            parallel->stages += translation.time;
        }
        parallel->count = translations.size();
    }
    size_t env_index{0};
    auto& programs{out.programs};
    const bool uses_vertex_a{key.unique_hashes[0] != 0};
    const bool uses_vertex_b{key.unique_hashes[1] != 0};

    // Layer passthrough generation for devices without VK_EXT_shader_viewport_index_layer
    Shader::IR::Program* layer_source_program{};
    bool mesh_unsupported{};
    // Converted instructions over all stages: the driver build time grows with it.
    size_t source_words{};

    for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {
        const bool is_emulated_stage = layer_source_program != nullptr &&
                                       index == static_cast<u32>(Maxwell::ShaderType::Geometry);
        if (key.unique_hashes[index] == 0 && is_emulated_stage) {
            auto topology = MaxwellToOutputTopology(key.state.topology);
            programs[index] = GenerateGeometryPassthrough(pools.inst, pools.block, host_info,
                                                          *layer_source_program, topology);
            continue;
        }
        if (key.unique_hashes[index] == 0) {
            continue;
        }
        Shader::Environment& env{*envs[env_index]};
        ++env_index;
        source_words += env.CodeWords();
        if (index == static_cast<size_t>(Maxwell::ShaderType::Geometry) &&
            !device.IsGeometryShaderSupported() && env.IsNativeProgram()) {
            // A native (Switch 2) geometry program on a device without geometry shaders: the
            // pipeline cannot run it, so its draws are skipped.
            static std::atomic<bool> logged{};
            if (!logged.exchange(true)) {
                LOG_WARNING(Render_Vulkan, "Skipping native geometry shader pipelines: the device has no geometry shaders");
            }
            return false;
        }
        // Without VK_EXT_mesh_shader the driver cannot build a task/mesh pipeline; the draw is
        // skipped (see the pipeline check in the rasterizer). The stages are still translated
        // so the skipped pipeline's needs can be reported below.
        if ((env.ShaderStage() == Shader::Stage::Task ||
             env.ShaderStage() == Shader::Stage::Mesh) &&
            !device.IsExtMeshShaderSupported()) {
            static std::atomic<bool> logged{};
            if (!logged.exchange(true, std::memory_order_relaxed)) {
                LOG_WARNING(Render_Vulkan,
                            "{} has no VK_EXT_mesh_shader: pipelines with task/mesh shaders are "
                            "skipped, so the geometry they draw (such as foliage and props) will "
                            "be missing",
                            device.GetModelName());
            }
            mesh_unsupported = true;
        }

        const auto translate = [&] {
            if (translated[index]) {
                return translated[index]->Take();
            }
            const u32 cfg_offset{
                static_cast<u32>(env.StartAddress() + sizeof(Shader::ProgramHeader))};
            Shader::Maxwell::Flow::CFG cfg(env, pools.flow_block, cfg_offset, index == 0);
            return TranslateProgram(pools.inst, pools.block, env, cfg, host_info);
        };
        if (!uses_vertex_a || index != 1) {
            // Normal path
            programs[index] = translate();
        } else {
            // VertexB path when VertexA is present.
            auto& program_va{programs[0]};
            auto program_vb{translate()};
            programs[index] = MergeDualVertexPrograms(program_va, program_vb, env, host_info);
            // Each half fit its texture arrays to the stage limits on its own; joined, they are
            // one stage.
            Shader::Optimization::LimitSampledTextureArrays(programs[index], host_info);
        }

        // Canonical native keys keep the attribute types the header's input map lists. A program
        // that loads another one would read it as disabled: this pipeline is dropped and the
        // program's keys keep every attribute type from now on.
        if (index == 1 && key.state.dynamic_vertex_input && env.IsNativeProgram() &&
            env.ShaderStage() == Shader::Stage::VertexB) {
            u32 loads = 0;
            for (u32 attribute = 0; attribute < 32; ++attribute) {
                if (programs[index].info.loads.Generic(attribute)) {
                    loads |= 1U << attribute;
                }
            }
            const u32 listed = VideoCommon::GenericReadMask(env.SPH());
            if ((loads & ~listed) != 0 && !IsUnmaskedVertexProgram(key.unique_hashes[1])) {
                {
                    std::scoped_lock lock{unmasked_mutex};
                    unmasked_vertex_programs.insert(key.unique_hashes[1]);
                    any_unmasked_vertex_program.store(true, std::memory_order_release);
                }
                LOG_ERROR(Render_Vulkan,
                          "Vertex program {:016x} loads attributes {:#010x} its header does not "
                          "list ({:#010x}); its pipelines now keep every attribute type",
                          key.unique_hashes[1], loads, listed);
                return false;
            }
        }

        if (device.GetDriverID() != VK_DRIVER_ID_NVIDIA_PROPRIETARY && Shader::Optimization::HasBrokenPattern(programs[index])) {
            LOG_WARNING(Render_Vulkan, "0x{:016x}: Skipping shader with known issues", key.unique_hashes[index]);
            return false;
        }

        if (Settings::values.dump_guest_shaders) {
            env.Dump(hash, key.unique_hashes[index]);
        }

        if (programs[index].info.requires_layer_emulation) {
            layer_source_program = &programs[index];
        }
    }
    // Mesh emulation without VK_EXT_mesh_shader (prototype): a mesh program, after an optional
    // task program, drawing triangles without per-primitive outputs, before a pixel program only.
    auto& mesh_emulation{out.mesh_emulation};
    size_t mesh_index{};
    if (mesh_unsupported) {
        ReportUnsupportedMeshPipeline(key, programs);
        const bool has_task{key.unique_hashes[1] != 0 &&
                            programs[1].stage == Shader::Stage::Task};
        for (size_t index = 1; index < Maxwell::MaxShaderProgram; ++index) {
            if (key.unique_hashes[index] != 0 && programs[index].stage == Shader::Stage::Mesh) {
                mesh_index = index;
                break;
            }
        }
        bool eligible{mesh_index != 0 && (mesh_index == 1 || has_task) &&
                      layer_source_program == nullptr};
        const Shader::IR::Program& mesh{programs[mesh_index]};
        // Points (0), lines (1) and triangles (4).
        const u32 topology{mesh.mesh.topology};
        eligible = eligible && (topology == 0 || topology == 1 || topology == 4) &&
                   mesh.mesh.primitive_lines.empty() &&
                   !mesh.mesh.vertex_lines.empty() && mesh.mesh.max_vertices != 0 &&
                   mesh.mesh.max_primitives != 0;
        for (size_t index = 1; index < Maxwell::MaxShaderProgram && eligible; ++index) {
            const bool emulated{index == mesh_index || (has_task && index == 1)};
            eligible = emulated || key.unique_hashes[index] == 0 ||
                       programs[index].stage == Shader::Stage::Fragment;
        }
        if (!eligible) {
            return false;
        }
        // The output buffer's binding follows every program's (unified bindings, one per
        // descriptor element, as the SPIR-V backend numbers them). The task and mesh programs
        // share the compute stage, so their descriptors and the emulation's own buffers count
        // against one set of per-stage limits.
        u32 output_binding{};
        u32 compute_storage_buffers{has_task ? 4U : 1U};
        u32 compute_sampled_images{};
        u32 compute_resources{compute_storage_buffers};
        for (size_t index = 1; index < Maxwell::MaxShaderProgram; ++index) {
            if (key.unique_hashes[index] == 0) {
                continue;
            }
            const Shader::Info& info{programs[index].info};
            const bool emulated{index == mesh_index || (has_task && index == 1)};
            const auto add = [&](const auto& descriptors, u32* category) {
                const u32 count{Shader::NumDescriptors(descriptors)};
                output_binding += count;
                if (emulated) {
                    compute_resources += count;
                    if (category) {
                        *category += count;
                    }
                }
            };
            add(info.constant_buffer_descriptors, nullptr);
            add(info.storage_buffers_descriptors, &compute_storage_buffers);
            add(info.texture_buffer_descriptors, &compute_sampled_images);
            add(info.image_buffer_descriptors, nullptr);
            add(info.texture_descriptors, &compute_sampled_images);
            add(info.image_descriptors, nullptr);
        }
        if (compute_storage_buffers > device.GetMaxPerStageDescriptorStorageBuffers() ||
            compute_sampled_images > device.GetMaxPerStageDescriptorSampledImages() ||
            compute_resources > device.GetMaxPerStageResources()) {
            LOG_WARNING(Render_Vulkan,
                        "Mesh emulation skipped: {} storage buffers, {} sampled images and {} "
                        "resources exceed the compute stage limits",
                        compute_storage_buffers, compute_sampled_images, compute_resources);
            return false;
        }
        mesh_emulation.emplace();
        mesh_emulation->output_binding = output_binding;
        if (has_task) {
            // After the output: the task payload, offset and argument buffers.
            mesh_emulation->has_task = true;
            mesh_emulation->mesh_stage = static_cast<u32>(mesh_index - 1);
            mesh_emulation->payload_stride = (std::max)(programs[1].mesh.payload_words, 2U);
            mesh_emulation->payload_binding = output_binding + 1;
            mesh_emulation->offsets_binding = output_binding + 2;
            mesh_emulation->args_binding = output_binding + 3;
            mesh_emulation->distribution_spirv =
                Shader::Backend::SPIRV::EmitMeshEmulationDistribution(profile);
        }
        mesh_emulation->block_words = Shader::Backend::SPIRV::MeshEmulationBlockWords(mesh.mesh);
        mesh_emulation->max_primitives = mesh.mesh.max_primitives;
        mesh_emulation->primitive_vertices = mesh.mesh.PrimitiveVertices();
        mesh_emulation->max_vertices = mesh.mesh.max_vertices;
        mesh_emulation->vertex_lines = static_cast<u32>(mesh.mesh.vertex_lines.size());
        for (u32 line = 0; line < mesh.mesh.vertex_lines.size(); ++line) {
            const u32 attribute{mesh.mesh.vertex_lines[line]};
            const u32 position_x{static_cast<u32>(Shader::IR::Attribute::PositionX)};
            if (attribute >= position_x && attribute < position_x + 4) {
                mesh_emulation->position_lines[attribute - position_x] = line;
            }
        }
        static std::atomic<u32> emulated{};
        if (const u32 count{++emulated}; count <= 64) {
            // The input region addresses the mesh program reads (0 is its workgroup index).
            std::string reads;
            for (const Shader::IR::Block* block : mesh.blocks) {
                for (const Shader::IR::Inst& inst : block->Instructions()) {
                    if (inst.GetOpcode() != Shader::IR::Opcode::IsbeRead) {
                        continue;
                    }
                    const Shader::IR::Value address{inst.Arg(0)};
                    reads += address.IsImmediate() ? fmt::format(" {:#x}", address.U32())
                                                   : std::string{" dynamic"};
                }
            }
            LOG_INFO(Render_Vulkan,
                     "Mesh emulation #{}: pipeline {:016x}, {} vertices x {} lines, {} triangles "
                     "per workgroup, output binding {}, task {} (payload {} words), input "
                     "reads:{}",
                     count, hash, mesh.mesh.max_vertices, mesh.mesh.vertex_lines.size(),
                     mesh.mesh.max_primitives, output_binding, has_task,
                     mesh_emulation->payload_stride, reads.empty() ? " none" : reads);
        }
    }
    // A tessellation pipeline the driver would refuse is skipped like a missing mesh pipeline:
    // the draw is dropped (see the pipeline check in the rasterizer) instead of failing the build.
    {
        const Shader::IR::Program* control{};
        const Shader::IR::Program* evaluation{};
        for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {
            if (key.unique_hashes[index] == 0) {
                continue;
            }
            if (programs[index].stage == Shader::Stage::TessellationControl) {
                control = &programs[index];
            } else if (programs[index].stage == Shader::Stage::TessellationEval) {
                evaluation = &programs[index];
            }
        }
        if (control != nullptr || evaluation != nullptr) {
            const u32 max_patch_size = device.GetMaxTessellationPatchSize();
            const char* reason = nullptr;
            if (!device.IsTessellationSupported()) {
                reason = "the device has no tessellation shaders";
            } else if (control == nullptr || evaluation == nullptr) {
                reason = "it has only one of the control and evaluation stages";
            } else if (control->invocations == 0 || control->invocations > max_patch_size) {
                reason = "the control shader's output vertex count is out of range";
            }
            if (reason != nullptr) {
                LOG_WARNING(Render_Vulkan,
                            "Skipping tessellation pipeline: {} (tcs={} tes={}, tcs output "
                            "vertices {}, patch control points {}, device max patch size {})",
                            reason, control != nullptr, evaluation != nullptr,
                            control != nullptr ? control->invocations : 0,
                            key.state.patch_control_points_minus_one.Value() + 1, max_patch_size);
                return false;
            }
        }
    }
    {
        // The descriptor set limits count the textures of every stage the pipeline emits.
        std::array<Shader::IR::Program*, Maxwell::MaxShaderProgram> stages{};
        size_t num_stages{};
        for (size_t index = uses_vertex_a && uses_vertex_b ? 1 : 0;
             index < Maxwell::MaxShaderProgram; ++index) {
            const bool is_emulated_stage = layer_source_program != nullptr &&
                                           index == static_cast<u32>(Maxwell::ShaderType::Geometry);
            if (key.unique_hashes[index] != 0 || is_emulated_stage) {
                stages[num_stages++] = &programs[index];
            }
        }
        Shader::Optimization::LimitPipelineSampledTextureArrays(
            std::span{stages.data(), num_stages}, host_info);
    }
    // EDEN_NX2_MESH_PIPELINE_LOG=1: the shape of each task/mesh pipeline (first 64).
    if (static const bool log_mesh = std::getenv("EDEN_NX2_MESH_PIPELINE_LOG") != nullptr;
        log_mesh) {
        static std::atomic<u32> logged{};
        for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {
            const auto& program = programs[index];
            if (key.unique_hashes[index] == 0 || !program.info.is_mesh ||
                logged.fetch_add(1, std::memory_order_relaxed) >= 64) {
                continue;
            }
            const bool has_task = std::ranges::any_of(programs, [](const auto& other) {
                return other.info.is_task;
            });
            LOG_INFO(Render_Vulkan,
                     "Mesh pipeline {:016x}: task {} mesh {:016x} fs {:016x}, {} threads, max "
                     "{} vertices {} primitives, payload {} words, {} vertex lines, depth {} "
                     "format {}",
                     key.Hash(), has_task, key.unique_hashes[index], key.unique_hashes[5],
                     program.workgroup_size[0], program.mesh.max_vertices,
                     program.mesh.max_primitives, program.mesh.payload_words,
                     program.mesh.vertex_lines.size(), key.state.depth_enabled.Value(),
                     key.state.depth_format.Value());
        }
    }
    // Ocarina of Time DLSS: object motion for pipelines that draw into a Z32 depth target
    // (docs/nx2/oot-motion-vectors-plan.md): vertex + fragment, or [task +] mesh + fragment.
    // Mesh programs evaluate their positions again with last frame's constant buffers
    // (OotShadowPass); EDEN_OOT_MESH_MOTION=history uses the per-vertex history instead (no task
    // stage, since a task's culling renumbers the mesh workgroups) and =0 turns mesh motion off.
    // Draws of other sizes disable it.
    static const char mesh_motion = [] {
        const char* value = std::getenv("EDEN_OOT_MESH_MOTION");
        return value != nullptr ? value[0] : 's';
    }();
    const bool mesh_shadow = mesh_motion == 's';
    const bool oot_z32 =
        VideoCommon::OotDlss::ObjectMotionRequested() &&
        (program_id != 0 ? program_id : Settings::GetCurrentProgramID()) ==
            VideoCommon::OotDlss::ProgramId &&
        key.state.depth_enabled != 0 &&
        key.state.depth_format == static_cast<u32>(Tegra::DepthFormat::Z32_FLOAT);
    const char* oot_reject = nullptr;
    if (oot_z32) {
        oot_reject = key.state.xfb_enabled != 0         ? "transform feedback"
                     : key.unique_hashes[5] == 0         ? "no fragment stage"
                     : layer_source_program != nullptr   ? "layer emulation"
                     : mesh_emulation                    ? "mesh emulation"
                     : !device.IsExtShaderAtomicInt64Supported() || !profile.support_int64
                         ? "no 64-bit atomics"
                         : nullptr;
    }
    if (oot_z32 && oot_reject == nullptr) {
        oot_reject = "unsupported stages";
        // The one stage that makes the vertices; every other slot but the fragment's is empty.
        size_t geometry = 0;
        bool shape_ok = true;
        for (size_t index = 0; index < Maxwell::MaxShaderProgram - 1; ++index) {
            if (key.unique_hashes[index] == 0) {
                continue;
            }
            const auto& info = programs[index].info;
            const bool vertex = programs[index].stage == Shader::Stage::VertexB && !info.is_task &&
                                !info.is_mesh;
            const bool mesh = info.is_mesh && (mesh_shadow || mesh_motion == 'h');
            if (geometry == 0 && (vertex || mesh) && !info.is_task) {
                geometry = index;
            } else if (info.is_task && mesh_shadow) {
                // The shadow evaluation does not depend on which workgroups the task launches.
            } else if (!(index == 0 && uses_vertex_b)) { // VertexA merged into VertexB
                shape_ok = false;
            }
        }
        Shader::Info& fragment = programs[5].info;
        if (shape_ok && geometry != 0 && programs[5].stage == Shader::Stage::Fragment) {
            oot_reject = nullptr;
            Shader::IR::Program& source = programs[geometry];
            Shader::Info& vertex = programs[geometry].info;
            const bool mesh = vertex.is_mesh;
            std::bitset<Shader::IR::NUM_GENERICS> used{};
            for (u32 location = 0; location < Shader::IR::NUM_GENERICS; ++location) {
                used[location] = vertex.stores.Generic(location);
            }
            if (mesh) {
                for (const auto& lines : {source.mesh.vertex_lines, source.mesh.primitive_lines}) {
                    for (const u32 line : lines) {
                        const auto attribute = static_cast<Shader::IR::Attribute>(line);
                        if (Shader::IR::IsGeneric(attribute)) {
                            used.set(Shader::IR::GenericAttributeIndex(attribute));
                        }
                    }
                }
            }
            std::array<u32, 2> free{};
            u32 found{};
            for (u32 location = Shader::IR::NUM_GENERICS; location-- > 0 && found < 2;) {
                if (!used[location]) {
                    free[found++] = location;
                }
            }
            const bool shadow_ok =
                !mesh || !mesh_shadow || Shader::Optimization::OotShadowPass(source);
            const auto stage_has_room = [&](const Shader::Info& info) {
                const u32 storage = Shader::NumDescriptors(info.storage_buffers_descriptors);
                const u32 resources = storage +
                    Shader::NumDescriptors(info.constant_buffer_descriptors) +
                    Shader::NumDescriptors(info.texture_buffer_descriptors) +
                    Shader::NumDescriptors(info.image_buffer_descriptors) +
                    Shader::NumDescriptors(info.texture_descriptors) +
                    Shader::NumDescriptors(info.image_descriptors);
                return storage < host_info.max_per_stage_descriptor_storage_buffers &&
                       resources < host_info.max_per_stage_resources;
            };
            u32 storage_buffers = 2; // The added history and motion buffers.
            for (size_t index = uses_vertex_a && uses_vertex_b ? 1 : 0;
                 index < Maxwell::MaxShaderProgram; ++index) {
                if (key.unique_hashes[index] != 0) {
                    storage_buffers +=
                        Shader::NumDescriptors(programs[index].info.storage_buffers_descriptors);
                }
            }
            oot_reject = found < 2                      ? "no two free varyings"
                         : !stage_has_room(vertex) || !stage_has_room(fragment) ||
                                   storage_buffers > host_info.max_descriptor_set_storage_buffers
                             ? "no room for motion descriptors"
                         : vertex.uses_render_area ||
                                   fragment.uses_render_area
                             ? "uses the render area"
                         : fragment.stores_frag_depth   ? "writes its own depth"
                         : vertex.stores.Legacy() || fragment.loads.Legacy()
                             ? "legacy attributes"
                         : !shadow_ok                   ? "mesh program the shadow pass can't evaluate"
                                                        : nullptr;
            if (oot_reject == nullptr) {
                for (Shader::Info* info : {&vertex, &fragment}) {
                    info->oot_motion.location_current = free[0];
                    info->oot_motion.location_previous = free[1];
                }
                vertex.oot_motion.history = true;
                vertex.oot_motion.mesh_max_vertices = mesh ? source.mesh.max_vertices : 0;
                fragment.oot_motion.image = true;
                fragment.uses_int64 = true;
                fragment.uses_int64_bit_atomics = true;
                const auto written = [](const auto& descriptors) {
                    return std::ranges::any_of(descriptors,
                                               [](const auto& desc) { return desc.is_written; });
                };
                fragment.oot_motion.early_tests =
                    !fragment.uses_demote_to_helper_invocation && !fragment.stores_global_memory &&
                    !written(fragment.storage_buffers_descriptors) &&
                    !written(fragment.image_descriptors) &&
                    !written(fragment.image_buffer_descriptors);
                static std::array<std::atomic<u32>, 2> logged{};
                if (logged[mesh].fetch_add(1, std::memory_order_relaxed) < 8) {
                    LOG_INFO(Render_Vulkan,
                             "OoT DLSS: object motion in pipeline {}={:016x} fs={:016x} "
                             "(varyings at {} and {}{})",
                             mesh ? "mesh" : "vs", key.unique_hashes[geometry],
                             key.unique_hashes[5], free[0], free[1],
                             vertex.oot_motion.shadow
                                 ? fmt::format(", shadow of {} constant buffer words",
                                               vertex.oot_motion.shadow_words)
                                 : std::string{});
                }
            }
        }
    }
    if (oot_reject != nullptr) {
        u64 geometry_hash = 0;
        for (size_t index = 1; index + 1 < Maxwell::MaxShaderProgram && geometry_hash == 0;
             ++index) {
            geometry_hash = key.unique_hashes[index];
        }
        VideoCommon::OotDlss::NoteMotionReject(geometry_hash, key.unique_hashes[5], oot_reject);
    }
    // Each stage stays within the limit on its own; together they share one descriptor set.
    if (const u32 set_limit = host_info.max_descriptor_set_storage_buffers; set_limit != 0) {
        size_t storage_buffers{};
        for (size_t index = uses_vertex_a && uses_vertex_b ? 1 : 0;
             index < Maxwell::MaxShaderProgram; ++index) {
            if (key.unique_hashes[index] != 0) {
                storage_buffers +=
                    Shader::NumDescriptors(programs[index].info.storage_buffers_descriptors);
                const auto& info = programs[index].info;
                storage_buffers += (info.oot_motion.history || info.oot_motion.image) ? 1 : 0;
            }
        }
        // The fallback binds its output buffer (and, for task draws, payload,
        // offsets and indirect arguments) into the same descriptor set.
        const size_t auxiliary_buffers{
            mesh_emulation ? (mesh_emulation->has_task ? 4U : 1U) : 0U};
        if (!FitsStorageBufferSetLimit(storage_buffers, auxiliary_buffers, set_limit)) {
            throw Shader::NotImplementedException(
                "Binding {} storage buffers ({} for mesh fallback) to a descriptor set limited to {}",
                storage_buffers + auxiliary_buffers, auxiliary_buffers, set_limit);
        }
    }
    // Graphics pipeline libraries (EDEN_NX2_GPL). Left on the monolithic route: task and mesh
    // pipelines, transform feedback, a static rasterizer discard, and stages whose texture or
    // image counts do not fit the split rescaling bits.
    out.use_libraries = allow_libraries && [&] {
        if (!device.IsExtGraphicsPipelineLibrarySupported() || key.state.xfb_enabled != 0) {
            return false;
        }
        if (key.state.extended_dynamic_state_2 == 0 &&
            key.state.dynamic_state.rasterize_enable == 0) {
            return false;
        }
        size_t pre_textures{}, pre_images{}, fragment_textures{}, fragment_images{};
        for (size_t index = 1; index < Maxwell::MaxShaderProgram; ++index) {
            const bool is_emulated_stage =
                layer_source_program != nullptr &&
                index == static_cast<u32>(Maxwell::ShaderType::Geometry);
            if (key.unique_hashes[index] == 0 && !is_emulated_stage) {
                continue;
            }
            const Shader::IR::Program& program{programs[index]};
            if (!mesh_warm_enabled &&
                (program.stage == Shader::Stage::Task || program.stage == Shader::Stage::Mesh)) {
                return false;
            }
            const bool fragment = index + 1 == Maxwell::MaxShaderProgram;
            (fragment ? fragment_textures : pre_textures) += program.info.texture_descriptors.size();
            (fragment ? fragment_images : pre_images) += program.info.image_descriptors.size();
        }
        return pre_textures <= GPL_FRAGMENT_TEXTURE_SCALING_BASE &&
               fragment_textures <= GPL_FRAGMENT_TEXTURE_SCALING_BASE &&
               pre_images <= GPL_FRAGMENT_IMAGE_SCALING_BASE &&
               fragment_images <= GPL_FRAGMENT_IMAGE_SCALING_BASE;
    }();
    out.source_words = source_words;
    const Shader::IR::Program* previous_stage{};
    Shader::Backend::Bindings binding;
    for (size_t index = uses_vertex_a && uses_vertex_b ? 1 : 0; index < Maxwell::MaxShaderProgram;
         ++index) {
        const bool is_emulated_stage = layer_source_program != nullptr &&
                                       index == static_cast<u32>(Maxwell::ShaderType::Geometry);
        if (key.unique_hashes[index] == 0 && !is_emulated_stage) {
            continue;
        }
        UNIMPLEMENTED_IF(index == 0);

        Shader::IR::Program& program{programs[index]};
        const size_t stage_index{index - 1};

        auto runtime_info{MakeRuntimeInfo(programs, key, program, previous_stage, device)};
        if (mesh_emulation && (program.stage == Shader::Stage::Mesh ||
                               program.stage == Shader::Stage::Task)) {
            runtime_info.mesh_as_compute = true;
            runtime_info.mesh_output_binding = mesh_emulation->output_binding;
            if (mesh_emulation->has_task) {
                runtime_info.mesh_task_payload_binding = mesh_emulation->payload_binding;
                runtime_info.mesh_task_offsets_binding = mesh_emulation->offsets_binding;
                runtime_info.mesh_task_args_binding = mesh_emulation->args_binding;
                runtime_info.mesh_task_payload_stride = mesh_emulation->payload_stride;
            }
            if (program.stage == Shader::Stage::Mesh) {
                mesh_emulation->vertex_spirv = Shader::Backend::SPIRV::EmitMeshEmulationVertex(
                    profile, program.mesh, mesh_emulation->output_binding,
                    mesh_emulation->has_task ? mesh_emulation->args_binding : ~0U,
                    runtime_info.convert_depth_mode && !profile.support_native_ndc,
                    runtime_info.fixed_state_point_size.value_or(1.0f));
            }
        }
        ConvertLegacyToGeneric(program, runtime_info);
        // EDEN_NX2_MESH_EMULATION_DUMP=<dir>: write the emulation's vertex shader for spirv-val.
        // (Emitting the mesh program a second time here to dump its compute shader crashed the
        // build that followed, so only the vertex shader is written.)
        if (mesh_emulation && program.stage == Shader::Stage::Mesh) {
            if (const char* const dump{std::getenv("EDEN_NX2_MESH_EMULATION_DUMP")}) {
                const auto& code{mesh_emulation->vertex_spirv};
                std::ofstream file{fmt::format("{}/{:016x}.vert.spv", dump, hash),
                                   std::ios::binary};
                file.write(reinterpret_cast<const char*>(code.data()),
                           static_cast<std::streamsize>(code.size() * sizeof(u32)));
            }
        }
        PrepareStageBindings(binding, stage_index, out.use_libraries);
        out.runtime_infos[stage_index] = runtime_info;
        if (reference_spirv && VideoCommon::Nx2Perf::Verify()) {
            out.reference_spirv[stage_index] = EmitSPIRV(profile, runtime_info, program, binding);
        }
        previous_stage = &program;
    }
    return true;
}

std::unique_ptr<GraphicsPipeline> PipelineCache::CreateGraphicsPipeline(
    ShaderPools& pools, const GraphicsPipelineCacheKey& key,
    std::span<Shader::Environment* const> envs, PipelineStatistics* statistics,
    bool build_in_parallel, std::shared_ptr<ShaderPools> emit_pools,
    ParallelTranslation* parallel, bool libraries_only, bool notify_runtime_build,
    DescriptorPool* transient_descriptors,
    std::array<std::vector<u32>, Maxwell::MaxShaderStage>* spirv_out) try {
    LOG_DEBUG(Render_Vulkan, "{:#016x}", key.Hash());
    // Declared before the programs, whose IR they hold
    std::vector<std::shared_ptr<ShaderPools>> stage_pools;
    SCOPE_EXIT {
        for (std::shared_ptr<ShaderPools>& stage : stage_pools) {
            ReturnPools(std::move(stage));
        }
    };
    GraphicsStages stages;
    if (!TranslateGraphicsStages(pools, key, envs, stages, emit_pools != nullptr, parallel,
                                 &stage_pools, spirv_out == nullptr)) {
        return nullptr;
    }
    auto& programs{stages.programs};
    const bool use_libraries = stages.use_libraries;
    if (libraries_only && !use_libraries) {
        return nullptr;
    }
    auto& mesh_emulation = stages.mesh_emulation;
    std::array<u128, Maxwell::MaxShaderStage> spirv_hashes{};
    u64 deferred_ir_bytes = 0;
    std::unique_ptr<QueuedIrBudget::Reservation> ir_reservation;
    if (emit_pools) {
        deferred_ir_bytes = IrBytes(programs);
        ir_reservation = queued_ir.TryReserve(deferred_ir_bytes);
    }
    const bool defer_emission = ir_reservation != nullptr;
    std::array<const Shader::Info*, Maxwell::MaxShaderStage> infos{};
    for (size_t stage = 0; stage < infos.size(); ++stage) {
        if (stages.runtime_infos[stage]) {
            infos[stage] = &programs[stage + 1].info;
        }
    }
    std::array<vk::ShaderModule, Maxwell::MaxShaderStage> modules;
    GraphicsPipeline::EmitStagesFunc emit_stages;
    if (defer_emission) {
        auto job = std::make_shared<DeferredStages>();
        job->reservation = std::move(ir_reservation);
        job->pools = emit_pools;
        job->stage_pools = std::move(stage_pools);
        stage_pools.clear();
        job->programs = std::move(programs);
        job->runtime_infos = std::move(stages.runtime_infos);
        job->reference_spirv = std::move(stages.reference_spirv);
        for (size_t stage = 0; stage < infos.size(); ++stage) {
            if (infos[stage]) {
                infos[stage] = &job->programs[stage + 1].info;
            }
        }
        emit_stages = [this, job = std::move(job), hashes = key.unique_hashes, use_libraries](
                          std::array<u128, Maxwell::MaxShaderStage>& stage_hashes) mutable {
            const auto begin = std::chrono::steady_clock::now();
            std::array<vk::ShaderModule, Maxwell::MaxShaderStage> stage_modules;
            Shader::Backend::Bindings stage_binding;
            for (size_t stage = 0; stage < stage_modules.size(); ++stage) {
                if (job->runtime_infos[stage]) {
                    PrepareStageBindings(stage_binding, stage, use_libraries);
                    stage_modules[stage] = EmitShaderModule(
                        device, profile, *job->runtime_infos[stage], job->programs[stage + 1],
                        stage_binding, hashes[stage + 1], stage, job->reference_spirv[stage],
                        use_libraries ? &stage_hashes[stage] : nullptr);
                }
            }
            auto completed_pools = std::move(job->pools);
            auto completed_stages = std::move(job->stage_pools);
            job.reset();
            ReturnPools(std::move(completed_pools));
            for (std::shared_ptr<ShaderPools>& stage : completed_stages) {
                ReturnPools(std::move(stage));
            }
            AddRuntimeSpirvTime(1, begin);
            return stage_modules;
        };
    } else {
        Shader::Backend::Bindings binding;
        for (size_t stage = 0; stage < modules.size(); ++stage) {
            if (!stages.runtime_infos[stage]) {
                continue;
            }
            const auto begin = std::chrono::steady_clock::now();
            PrepareStageBindings(binding, stage, use_libraries);
            modules[stage] = EmitShaderModule(device, profile, *stages.runtime_infos[stage],
                                              programs[stage + 1], binding,
                                              key.unique_hashes[stage + 1], stage, {},
                                              use_libraries ? &spirv_hashes[stage] : nullptr,
                                              spirv_out ? &(*spirv_out)[stage] : nullptr);
            if (build_in_parallel) {
                AddRuntimeSpirvTime(0, begin);
            }
        }
    }
    const bool native_mesh{!mesh_emulation && infos[0] != nullptr &&
                           (infos[0]->is_task || infos[0]->is_mesh)};
    GraphicsPipeline::LibraryParams library_params{
        .library_cache = use_libraries && !mesh_emulation ? &graphics_libraries : nullptr,
        .spirv_hashes = spirv_hashes,
        .libraries_only = libraries_only,
    };
    // A libraries_only build (warm-up) is built here, on the calling thread: no job, and not left
    // for Build() either, so Failed() is known when this returns.
    const PipelineBuildQueue build_queue =
        libraries_only ? PipelineBuildQueue{}
                       : BuildQueue(build_in_parallel, defer_emission ? deferred_ir_bytes : 0,
                                    native_mesh);
    static_assert(VideoCommon::EvictedPointerSlots::NumPrograms == Maxwell::MaxShaderProgram);
    VideoCommon::EvictedPointerSlots::Slots mapped_pointers;
    for (size_t index = 0, mapped_env = 0; index < Maxwell::MaxShaderProgram; ++index) {
        if (key.unique_hashes[index] != 0) {
            mapped_pointers[index] = envs[mapped_env++]->MappedStoragePointerKeys();
        }
    }
    auto pipeline = std::make_unique<GraphicsPipeline>(
        scheduler, buffer_cache, texture_cache, vulkan_pipeline_cache, pipeline_binaries.get(),
        libraries_only || !notify_runtime_build ? nullptr : &shader_notify, device,
        transient_descriptors ? *transient_descriptors : descriptor_pool,
        guest_descriptor_queue, descriptor_buffer_ring, build_queue, statistics,
        render_pass_cache, key, std::move(modules), infos, std::move(emit_stages),
        std::move(stages.mesh_emulation), library_params);
    pipeline->SetSourceWords(stages.source_words);
    pipeline->SetMappedPointers(std::move(mapped_pointers));
    return pipeline;

} catch (const Shader::Exception& exception) {
    auto hash = key.Hash();
    size_t env_index{0};
    for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {
        if (key.unique_hashes[index] == 0) {
            continue;
        }
        Shader::Environment& env{*envs[env_index]};
        ++env_index;

        const u32 cfg_offset{static_cast<u32>(env.StartAddress() + sizeof(Shader::ProgramHeader))};
        try {
            // Walks the program for the dump; the one that failed may fail here again, and an
            // exception leaving this handler would end the process.
            Shader::Maxwell::Flow::CFG cfg(env, pools.flow_block, cfg_offset, index == 0);
        } catch (const Shader::Exception&) {
        }
        env.Dump(hash, key.unique_hashes[index]);
    }
    LOG_ERROR(Render_Vulkan, "{}", exception.what());
    return nullptr;
}

namespace {
// Pipelines built on the GPU thread at run time (shader translation, and the SPIR-V unless a worker
// emits it; the driver compile is on workers): the first 400 over 2 ms and every one over 100 ms
// are logged, and the totals every 10 s, to tell how much of the stutter on new content is
// translation.
void ReportRuntimePipeline(const char* kind, std::chrono::steady_clock::time_point begin,
                           const std::string& shaders,
                           std::chrono::nanoseconds translation_wait = {},
                           std::chrono::nanoseconds stage_translation = {}) {
    static std::mutex mutex;
    std::scoped_lock lock{mutex};
    static std::chrono::steady_clock::duration total{};
    static std::chrono::nanoseconds total_translation_wait{}, total_stage_translation{};
    static u32 count{}, logged{};
    static auto window = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - begin;
    total += elapsed;
    total_translation_wait += translation_wait;
    total_stage_translation += stage_translation;
    ++count;
    const auto ms = [](auto d) { return std::chrono::duration<double, std::milli>(d).count(); };
    // Slow builds are always logged with their shaders, to find the programs that are slow to
    // translate. EDEN_NX2_PIPELINE_LOG_LIMIT=<n> changes how many of the others are.
    static const u32 limit = [] {
        const char* value = std::getenv("EDEN_NX2_PIPELINE_LOG_LIMIT");
        return value ? static_cast<u32>(std::strtoul(value, nullptr, 10)) : 400U;
    }();
    if (elapsed > std::chrono::milliseconds{100} ||
        (elapsed > std::chrono::milliseconds{2} && logged < limit)) {
        ++logged;
        LOG_INFO(Render_Vulkan, "{} pipeline built on the GPU thread in {:.1f} ms: {}", kind,
                 ms(elapsed), shaders);
    }
    if (now - window > std::chrono::seconds{10}) {
        const auto take = [](auto& counter) {
            return std::chrono::nanoseconds{counter.exchange(0, std::memory_order::relaxed)};
        };
        // Shader cache v2: the GPU thread's wait for graphics stages translated at once on
        // workers, the translation time they took together, and compute SPIR-V on workers
        const std::string v2 = fmt::format(
            ", {:.1f} ms on the GPU thread waiting for {:.1f} ms of graphics "
            "stage translation on workers, {:.1f} ms compute SPIR-V on workers",
            ms(total_translation_wait), ms(total_stage_translation), ms(take(runtime_spirv_ns[2])));
        LOG_INFO(Render_Vulkan,
                 "Runtime pipelines in the last 10 s: {} built, {:.1f} ms on the GPU thread "
                 "({:.1f} ms of it graphics SPIR-V), {:.1f} ms graphics SPIR-V on workers, "
                 "{:.1f} ms of draws held for unbuilt pipelines{}",
                 count, ms(total), ms(take(runtime_spirv_ns[0])), ms(take(runtime_spirv_ns[1])),
                 ms(GraphicsPipeline::TakeHoldTime()), v2);
        std::string reasons;
        {
            std::scoped_lock reasons_lock{precompiled_reasons_mutex};
            for (const auto& [reason, n] : precompiled_reasons) {
                reasons += fmt::format(", {} {}", n, reason);
            }
            precompiled_reasons.clear();
        }
        LOG_INFO(Render_Vulkan,
                 "Precompiled pipelines in the last 10 s: {} created from kept infos ({} of them "
                 "from stage infos, {:.1f} ms on the GPU thread), {} built from kept SPIR-V, {} "
                 "translated on workers from their recorded environments, {} failed and "
                 "replaced{}",
                 precompiled_created.exchange(0, std::memory_order::relaxed),
                 precompiled_per_stage.exchange(0, std::memory_order::relaxed),
                 ms(std::chrono::nanoseconds{
                     precompiled_gpu_ns.exchange(0, std::memory_order::relaxed)}),
                 precompiled_from_spirv.exchange(0, std::memory_order::relaxed),
                 precompiled_drawn.exchange(0, std::memory_order::relaxed),
                 precompiled_failed.exchange(0, std::memory_order::relaxed), reasons);
        if (PrecompiledAudit()) {
            LOG_INFO(Render_Vulkan,
                     "Precompiled audit in the last 10 s: {} pipelines translated from the game's "
                     "state gave the kept SPIR-V and infos, {} differed",
                     audit_same.exchange(0, std::memory_order::relaxed),
                     audit_differ.exchange(0, std::memory_order::relaxed));
        }
        if (HeavySkipWords() != 0) {
            LOG_INFO(Render_Vulkan,
                     "Nx2Perf heavy_skip in the last 10 s: {} draw lookups skipped for {} heavy "
                     "pipelines; held instead: {} (writes storage), {} (target not cleared this frame), "
                     "{} (new small target)",
                     heavy_skip_counters.draws, heavy_skip_counters.pipelines,
                     heavy_skip_counters.held_storage, heavy_skip_counters.held_not_cleared,
                     heavy_skip_counters.held_new_target);
            heavy_skip_counters = {};
        }
        total = {};
        total_translation_wait = {};
        total_stage_translation = {};
        count = 0;
        window = now;
    }
}
} // Anonymous namespace

void PipelineCache::WarnKeptPointer(size_t program, u64 slot) const {
    if ((slot >> 63) != 0) {
        return;
    }
    const u32 index = static_cast<u32>(slot >> 32);
    const u32 offset = static_cast<u32>(slot);
    const size_t stage = program == 0 ? 0 : program - 1;
    const auto& cbufs = maxwell3d->state.shader_stages[stage].const_buffers;
    GPUVAddr value = 0;
    const char* reason = nullptr;
    if (index >= cbufs.size() || !cbufs[index].enabled ||
        offset + sizeof(u64) > cbufs[index].size ||
        !gpu_memory->IsFullyMappedRange(cbufs[index].address + offset, sizeof(u64))) {
        reason = "constant buffer not bound";
    } else {
        value = gpu_memory->Read<u64>(cbufs[index].address + offset);
        if (value == 0) {
            reason = "null";
        } else if (!gpu_memory->IsWithinGPUAddressRange(value)) {
            reason = "outside the GPU address range";
        } else if (!gpu_memory->IsFullyMappedRange(value, 1)) {
            reason = "unmapped";
        }
    }
    if (!reason) {
        return;
    }
    static std::atomic<u64> kept{};
    const u64 count = kept.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 32 || count % 1000 == 0) {
        LOG_WARNING(Render_Vulkan,
                    "Graphics pipeline {:016x} translated again after eviction: s{}:c{}[{:#x}] "
                    "holds {:#x} ({}), kept as the storage buffer pointer it was ({} so far)",
                    graphics_key.Hash(), program, index, offset, value, reason, count);
    }
}

std::unique_ptr<GraphicsPipeline> PipelineCache::CreateGraphicsPipeline(
    const VideoCommon::EvictedPointerSlots::Slots* keep_pointers) {
    const VideoCommon::Nx2Profile::Scope profile_scope{
        VideoCommon::Nx2Profile::Zone::GraphicsPipelineMiss};
    const auto begin = std::chrono::steady_clock::now();
    std::optional<ParallelTranslation> parallel;
    SCOPE_EXIT {
        std::string shaders = fmt::format("pipeline={:016x}", graphics_key.Hash());
        for (size_t stage = 0; stage < graphics_key.unique_hashes.size(); ++stage) {
            if (graphics_key.unique_hashes[stage] != 0) {
                shaders += fmt::format(" s{}:{:016x}", stage, graphics_key.unique_hashes[stage]);
            }
        }
        if (!parallel) {
            ReportRuntimePipeline("Graphics", begin, shaders);
            return;
        }
        const auto ms = [](auto d) { return std::chrono::duration<double, std::milli>(d).count(); };
        shaders += fmt::format(" translate_wait_ms={:.1f} stage_translate_ms={:.1f} stages={}",
                               ms(parallel->wait), ms(parallel->stages), parallel->count);
        ReportRuntimePipeline("Graphics", begin, shaders, parallel->wait, parallel->stages);
    };
    GraphicsEnvironments environments;
    GetGraphicsEnvironments(environments, graphics_key.unique_hashes);
    if (keep_pointers) {
        for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {
            if (graphics_key.unique_hashes[index] == 0) {
                continue;
            }
            environments.envs[index].KeepStoragePointers((*keep_pointers)[index]);
            for (const u64 slot : (*keep_pointers)[index]) {
                WarnKeptPointer(index, slot);
            }
        }
    }

    const auto precompiled_begin = std::chrono::steady_clock::now();
    std::optional<PrecompiledRecord> precompiled_record;
    auto pipeline{CreatePrecompiledGraphicsPipeline(environments, &precompiled_record)};
    if (pipeline) {
        precompiled_created.fetch_add(1, std::memory_order_relaxed);
        precompiled_gpu_ns.fetch_add(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                                                 precompiled_begin)
                .count(),
            std::memory_order_relaxed);
        if (PrecompiledAudit()) {
            AuditPrecompiled(environments.Span(), false, graphics_key.Hash(),
                             precompiled_record ? &*precompiled_record : nullptr);
        }
    } else {
    // Texture descriptors are read without a flush (ReadTextureInfo), so parallel translation
    // never submits through the GPU thread's scheduler or needs a pool-wide flush.
    if (std::ranges::count_if(graphics_key.unique_hashes, [](u64 hash) { return hash != 0; }) >=
        2) {
        parallel.emplace();
    }

        // Native programs emit their SPIR-V in the pipeline's job instead of here
        std::shared_ptr<ShaderPools> emit_pools;
        if (maxwell3d->GetNativeMacroEngine().native_turing) {
            emit_pools = TakeSparePools();
        } else {
            main_pools.ReleaseContents();
        }
        ShaderPools& pools = emit_pools ? *emit_pools : main_pools;
        pipeline = CreateGraphicsPipeline(pools, graphics_key, environments.Span(), nullptr, true,
                                          std::move(emit_pools), parallel ? &*parallel : nullptr);
    }
    if (!pipeline || pipeline_cache_filename.empty()) {
        return pipeline;
    }
    if (disk_pipelines.contains(graphics_key.Hash())) {
        // In the disk cache already, only not built at boot.
        QueueVulkanPipelineCacheFlush();
        return pipeline;
    }
    disk_pipelines.insert(graphics_key.Hash());
    serialization_thread.QueueWork([this, key = graphics_key, envs = std::move(environments.envs)] {
        boost::container::static_vector<const GenericEnvironment*, Maxwell::MaxShaderProgram>
            env_ptrs;
        for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {
            if (key.unique_hashes[index] != 0) {
                env_ptrs.push_back(&envs[index]);
            }
        }
        const std::vector<u8> payload{VideoCommon::SerializePipelinePayload(
            std::span(reinterpret_cast<const char*>(&key), sizeof(key)), env_ptrs,
            host_info.EnvironmentReadProperties())};
        if (!payload.empty()) {
            cache_v2->Writer().Append(VideoCommon::ShaderCacheV2::RecordKind::Graphics,
                                      GraphicsRecordKey(key), payload);
        }
    });
    QueueVulkanPipelineCacheFlush();
    return pipeline;
}

ComputePipelineCacheEntry PipelineCache::CreateComputePipeline(const ComputePipelineCacheKey& key,
                                                               const ShaderInfo* shader,
                                                               std::span<const u64> keep_pointers) {
    const VideoCommon::Nx2Profile::Scope profile_scope{
        VideoCommon::Nx2Profile::Zone::ComputePipelineMiss};
    const auto begin = std::chrono::steady_clock::now();
    SCOPE_EXIT {
        ReportRuntimePipeline("Compute", begin,
                              fmt::format("pipeline={:016x} s6:{:016x} address={:#x}", key.Hash(),
                                          key.unique_hash,
                                          kepler_compute->regs.code_loc.Address() +
                                              kepler_compute->launch_description.program_start));
    };
    const GPUVAddr program_base{kepler_compute->regs.code_loc.Address()};
    const auto& qmd{kepler_compute->launch_description};
    ComputeEnvironment env{*kepler_compute, *gpu_memory, program_base, qmd.program_start,
                           shader->lowered_native};
    env.SetCachedSize(shader->size_bytes);
    env.KeepStoragePointers(keep_pointers);

    // A rebuild too (keep_pointers): its environment answers the kept pointers as pointers before
    // anything is asked, as its translation would see them.
    ComputePipelineCacheEntry entry;
    {
        const auto precompiled_begin = std::chrono::steady_clock::now();
        const PrecompiledRecord* matched = nullptr;
        entry = CreatePrecompiledComputePipeline(key, env, &matched);
        if (entry.pipeline && PrecompiledAudit()) {
            Shader::Environment* const env_ptr = &env;
            AuditPrecompiled(std::span(&env_ptr, 1), true, key.Hash(), matched);
        }
        if (entry.pipeline) {
            precompiled_created.fetch_add(1, std::memory_order_relaxed);
            precompiled_gpu_ns.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                             std::chrono::steady_clock::now() - precompiled_begin)
                                             .count(),
                                         std::memory_order_relaxed);
        }
    }
    if (!entry.pipeline) {
        // The SPIR-V is emitted by the pipeline's job instead of here
        std::shared_ptr<ShaderPools> emit_pools = TakeSparePools();
        ShaderPools& pools = *emit_pools;
        entry = CreateComputePipeline(pools, key, env, nullptr, true, std::move(emit_pools));
    }
    if (const char* directory = std::getenv("EDEN_NX2_SHADER_DUMP");
        directory && !entry.pipeline && env.CanBeSerialized()) {
        const auto path = std::filesystem::path{directory} /
                          fmt::format("failed-compute-{:016x}-v{}.env", key.unique_hash, CACHE_VERSION);
        std::ofstream file{path, std::ios::binary};
        if (file) {
            env.Serialize(file, host_info.EnvironmentReadProperties());
            LOG_INFO(Render_Vulkan, "Captured failed compute environment: {} written={}",
                     path.string(), file.good());
        }
    }
    if (!entry.pipeline || pipeline_cache_filename.empty()) {
        return entry;
    }
    serialization_thread.QueueWork([this, key, env_ = std::move(env)] {
        const std::array<const GenericEnvironment*, 1> envs{&env_};
        const std::vector<u8> payload{VideoCommon::SerializePipelinePayload(
            std::span(reinterpret_cast<const char*>(&key), sizeof(key)), envs,
            host_info.EnvironmentReadProperties())};
        if (!payload.empty()) {
            cache_v2->Writer().Append(VideoCommon::ShaderCacheV2::RecordKind::Compute,
                                      ComputeRecordKey(payload), payload);
        }
    });
    QueueVulkanPipelineCacheFlush();
    return entry;
}


ComputePipelineCacheEntry PipelineCache::CreateComputePipeline(
    ShaderPools& pools, const ComputePipelineCacheKey& key, Shader::Environment& env,
    PipelineStatistics* statistics, bool build_in_parallel,
    std::shared_ptr<ShaderPools> emit_pools, std::vector<u32>* spirv_out,
    DescriptorPool* transient_descriptors) try {
    SCOPE_EXIT {
        if (emit_pools) {
            ReturnPools(std::move(emit_pools));
        }
    };
    auto hash = key.Hash();
    if (device.HasBrokenCompute()) {
        LOG_ERROR(Render_Vulkan, "Skipping {:#016x}", hash);
        return {.failure = "Device reports broken compute support"};
    }

    LOG_DEBUG(Render_Vulkan, "{:#016x}", hash);

    Shader::Maxwell::Flow::CFG cfg{env, pools.flow_block, env.StartAddress()};

    // Dump it before error.
    if (Settings::values.dump_guest_shaders) {
        env.Dump(hash, key.unique_hash);
    }

    auto program{TranslateProgram(pools.inst, pools.block, env, cfg, host_info)};
    const VkDriverIdKHR driver_id = device.GetDriverID();
    // Portable caches already contain lowered native code, bypassing the SASS rejection policy.
    // Cache loading precedes engine binding; use the same device/title policy without an engine.
    const bool uses_tensor_stand_in = !VideoCommon::EmulatesNativeTensorPrograms(
        Settings::values.tensor_programs.GetValue(), Settings::GetCurrentProgramID(),
        SupportsCooperativeHmma());
    if (env.IsNativeProgram() && uses_tensor_stand_in) {
        for (const auto* block : program.blocks) {
            for (const auto& inst : block->Instructions()) {
                if (inst.GetOpcode() == Shader::IR::Opcode::TensorMulAdd) {
                    return {.failure = "Native matrix multiply uses this host's upscale stand-in"};
                }
            }
        }
    }
    const bool needs_shared_mem_clamp =
        driver_id == VK_DRIVER_ID_QUALCOMM_PROPRIETARY ||
        driver_id == VK_DRIVER_ID_ARM_PROPRIETARY || driver_id == VK_DRIVER_ID_MOLTENVK;
    const u32 max_shared_memory = device.GetMaxComputeSharedMemorySize();
    if (needs_shared_mem_clamp && program.shared_memory_size > max_shared_memory) {
        LOG_WARNING(Render_Vulkan,
                    "Compute shader {:#016x} requests {}KB shared memory but device max is {}KB - clamping",
                    key.unique_hash,
                    program.shared_memory_size / 1024,
                    max_shared_memory / 1024);
        program.shared_memory_size = max_shared_memory;
    }
    std::unique_ptr<ComputePipeline> pipeline;
    const u64 ir_bytes = emit_pools ? IrBytes(std::span(&program, 1)) : 0;
    if (auto ir_reservation = emit_pools ? queued_ir.TryReserve(ir_bytes) : nullptr) {
        auto job = std::make_shared<DeferredCompute>();
        if (VideoCommon::Nx2Perf::Verify()) {
            job->reference_spirv = EmitComputeShader(profile, program, key.unique_hash);
        }
        job->reservation = std::move(ir_reservation);
        job->pools = std::move(emit_pools);
        job->program = std::move(program);
        // Copied by the pipeline before its job can run
        const Shader::Info& info = job->program.info;
        auto emit = [this, job, shader_hash = key.unique_hash]() mutable {
            const auto begin = std::chrono::steady_clock::now();
            vk::ShaderModule module{EmitComputeModule(device, profile, job->program, shader_hash,
                                                      job->reference_spirv)};
            auto completed_pools = std::move(job->pools);
            job.reset();
            ReturnPools(std::move(completed_pools));
            AddRuntimeSpirvTime(2, begin);
            return module;
        };
        pipeline = std::make_unique<ComputePipeline>(
            device, scheduler, vulkan_pipeline_cache, pipeline_binaries.get(), hash,
            transient_descriptors ? *transient_descriptors : descriptor_pool,
            guest_descriptor_queue, descriptor_buffer_ring,
            BuildQueue(build_in_parallel, ir_bytes), statistics,
            &shader_notify, info, vk::ShaderModule{}, key.unique_hash, std::move(emit));
    } else {
        vk::ShaderModule module{
            EmitComputeModule(device, profile, program, key.unique_hash, {}, spirv_out)};
        const Shader::Info info{std::move(program.info)};
        if (!build_in_parallel) {
            // The driver compiles on this thread. Precache and boot workers would otherwise hold their
            // IR through the compile, up to hundreds of MiB for one large shader.
            program = {};
            pools.block.Free();
            pools.inst.Free();
        }
        pipeline = std::make_unique<ComputePipeline>(
            device, scheduler, vulkan_pipeline_cache, pipeline_binaries.get(), hash,
            transient_descriptors ? *transient_descriptors : descriptor_pool,
            guest_descriptor_queue, descriptor_buffer_ring,
            BuildQueue(build_in_parallel), statistics, &shader_notify, info, std::move(module),
            key.unique_hash);
    }
    return {.pipeline = std::move(pipeline),
            .null_pointer_slots = env.IsNativeProgram() ? env.NullStoragePointerSlots()
                                                        : std::vector<std::pair<u32, u32>>{},
            .mapped_pointers =
                env.IsNativeProgram() ? env.MappedStoragePointerKeys() : std::vector<u64>{}};

} catch (const Shader::Exception& exception) {
    LOG_ERROR(Render_Vulkan, "Compute pipeline compilation failed shader={:#016x} pipeline={:#016x}: {}",
              key.unique_hash, key.Hash(), exception.what());
    return {.failure = exception.what()};
}

bool PipelineCache::SerializeVulkanPipelineCache(const std::filesystem::path& filename,
                                                 const vk::PipelineCache& pipeline_cache,
                                                 u32 cache_version) {
    auto temporary = filename;
    temporary += ".tmp";
    try {
        if (!pipeline_cache) {
            return false;
        }
        std::vector<char> cache_data;
        size_t cache_size{};
        VkResult result = VK_INCOMPLETE;
        // Runtime builds can grow the cache between its size query and the data read.
        for (size_t attempt = 0; attempt < 3 && result == VK_INCOMPLETE; ++attempt) {
            result = pipeline_cache.Read(&cache_size, nullptr);
            if (result != VK_SUCCESS || cache_size == 0) {
                break;
            }
            cache_data.resize(cache_size);
            result = pipeline_cache.Read(&cache_size, cache_data.data());
        }
        if (result != VK_SUCCESS || cache_size == 0 || cache_size > cache_data.size()) {
            LOG_ERROR(Render_Vulkan, "Failed to read Vulkan driver pipeline cache: {}",
                      static_cast<int>(result));
            return false;
        }
        // Publish only a complete snapshot. A failed write leaves the previous cache intact.
        std::ofstream file;
        file.exceptions(std::ofstream::failbit | std::ofstream::badbit);
        file.open(temporary, std::ios::binary | std::ios::trunc);
        file.write(VULKAN_CACHE_MAGIC_NUMBER.data(), VULKAN_CACHE_MAGIC_NUMBER.size())
            .write(reinterpret_cast<const char*>(&cache_version), sizeof(cache_version))
            .write(cache_data.data(), cache_size);
        file.close();
        std::filesystem::rename(temporary, filename);
        LOG_INFO(Render_Vulkan, "Vulkan driver pipelines cached at: {}",
                 Common::FS::PathToUTF8String(filename));
        return true;
    } catch (const std::exception& exception) {
        LOG_ERROR(Common_Filesystem, "Failed to save Vulkan driver pipeline cache {}: {}",
                  Common::FS::PathToUTF8String(filename), exception.what());
        std::error_code error;
        std::filesystem::remove(temporary, error);
        return false;
    }
}

vk::PipelineCache PipelineCache::LoadVulkanPipelineCache(const std::filesystem::path& filename,
                                                         u32 expected_cache_version) {
    const auto create_pipeline_cache = [this](size_t data_size, const void* data) {
        VkPipelineCacheCreateInfo pipeline_cache_ci = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .initialDataSize = data_size,
            .pInitialData = data};
        return device.GetLogical().CreatePipelineCache(pipeline_cache_ci);
    };
    try {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return create_pipeline_cache(0, nullptr);
        }
        file.exceptions(std::ifstream::failbit);
        const auto end{file.tellg()};
        file.seekg(0, std::ios::beg);

        std::array<char, 8> magic_number;
        u32 cache_version;
        // A file cut short (the emulator stopped while writing it) would throw from the reads
        // below, and macOS's libc++ throws an ios_base::failure the handler doesn't match.
        if (static_cast<size_t>(end) < magic_number.size() + sizeof(cache_version)) {
            file.close();
            LOG_ERROR(Common_Filesystem, "Truncated Vulkan driver pipeline cache file, deleting it");
            Common::FS::RemoveFile(filename);
            return create_pipeline_cache(0, nullptr);
        }
        file.read(magic_number.data(), magic_number.size())
            .read(reinterpret_cast<char*>(&cache_version), sizeof(cache_version));
        if (magic_number != VULKAN_CACHE_MAGIC_NUMBER || cache_version != expected_cache_version) {
            file.close();
            if (Common::FS::RemoveFile(filename)) {
                if (magic_number != VULKAN_CACHE_MAGIC_NUMBER) {
                    LOG_ERROR(Common_Filesystem, "Invalid Vulkan driver pipeline cache file");
                }
                if (cache_version != expected_cache_version) {
                    LOG_INFO(Common_Filesystem, "Deleting old Vulkan driver pipeline cache");
                }
            } else {
                LOG_ERROR(Common_Filesystem,
                          "Invalid Vulkan pipeline cache file and failed to delete it in \"{}\"",
                          Common::FS::PathToUTF8String(filename));
            }
            return create_pipeline_cache(0, nullptr);
        }

        static constexpr size_t header_size = magic_number.size() + sizeof(cache_version);
        const size_t cache_size = static_cast<size_t>(end) - header_size;
        std::vector<char> cache_data(cache_size);
        file.read(cache_data.data(), cache_size);

        LOG_INFO(Render_Vulkan,
                 "Loaded Vulkan driver pipeline cache: {}", Common::FS::PathToUTF8String(filename));

        try {
            return create_pipeline_cache(cache_size, cache_data.data());
        } catch (const vk::Exception& e) {
            // The blob came from another driver or driver version (e.g. switching between
            // MoltenVK and KosmicKrisp on macOS): the driver rejects it. Start empty instead of
            // aborting, and drop the stale file so the next run doesn't retry it.
            LOG_ERROR(Render_Vulkan,
                      "Driver rejected the Vulkan pipeline cache file ({}), starting with an empty "
                      "cache and deleting it",
                      e.what());
            Common::FS::RemoveFile(filename);
            return create_pipeline_cache(0, nullptr);
        }

    } catch (const std::ios_base::failure& e) {
        LOG_ERROR(Common_Filesystem, "{}", e.what());
        if (!Common::FS::RemoveFile(filename)) {
            LOG_ERROR(Common_Filesystem, "Failed to delete Vulkan driver pipeline cache file {}",
                      Common::FS::PathToUTF8String(filename));
        }

        return create_pipeline_cache(0, nullptr);
    }
}

} // namespace Vulkan
