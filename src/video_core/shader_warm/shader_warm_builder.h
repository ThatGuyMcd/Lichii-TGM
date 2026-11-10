// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "common/common_types.h"
#include "common/settings_enums.h"

namespace Core {
class System;
}

namespace VideoCommon::ShaderWarm {

/// Where the warm-up files of a title live: next to its pipeline cache
/// (<shader dir>/<title id>/warm-compute.bin, warm-graphics.bin and warm-mesh.bin).
/// EDEN_NX2_COMPUTE_WARM / EDEN_NX2_GRAPHICS_WARM / EDEN_NX2_MESH_WARM override these paths.
std::filesystem::path ComputeWarmPath(u64 title_id);

/// The title's graphics precache file (<shader dir>/<title id>/warm-graphics.bin): the vertex
/// and fragment programs found in the game files, as single-stage pipeline cache entries with
/// guessed draw state. Built as pipeline libraries, so it needs graphics pipeline library
/// support.
std::filesystem::path GraphicsWarmPath(u64 title_id);
std::filesystem::path MeshWarmPath(u64 title_id);
/// The title's precompile recipe (tools/nx2-precompile), kept once a build used it, and the
/// pipeline file assembled from it with the player's game files, which the loading screen prepares.
std::filesystem::path NativeRecipePath(u64 title_id);
std::filesystem::path NativePreparationPath(u64 title_id);

/// The recipe for this title: the kept one, else the first <exe dir>/recipes/*.nx2recipe whose
/// header names this title and this build's cache version (as Fritchii ships them). Empty: none.
std::filesystem::path FindNativeRecipe(u64 title_id);

/// "Already precached" marker of a precache file (same folder, extension .done), written by the
/// emulator after the precache ran to the end. A matching marker skips the precache at boot.
std::filesystem::path WarmMarkerPath(const std::filesystem::path& warm_file);

/// Entry count of a precache file, written by the builder next to it (extension .count): the
/// entry count and the file's size. The file format has no count, so the loading screen shows
/// exact progress only with this.
std::filesystem::path WarmCountPath(const std::filesystem::path& warm_file);

/// The entry count recorded for `warm_file`, or 0 when there is none or it was written for a
/// file of another size.
u64 ReadWarmCount(const std::filesystem::path& warm_file);

/// What a marker records: the precache file's size and last-write time, its entry count, the GPU
/// driver version and device ID, the pipeline cache version and whether shared shader functions
/// were built as calls (compute bit 0, graphics bit 1: the SPIR-V the driver compiled differs),
/// and the graphics link mode (bit 0 fast links usable, bit 1 optimized links on: together they
/// decide whether libraries keep link time optimization info, which changes what the driver
/// caches). Markers written without the last field no longer match, so that precache runs once
/// more.
struct WarmStamp {
    u64 size{};
    s64 write_time{};
    u64 entries{};
    u32 driver{};
    u32 device_id{};
    u32 cache_version{};
    u32 no_inline{};
    u32 link_mode{};
    std::string configuration{"persist1"};

    /// True when `other` was written for the same file, driver, device, cache version, shader
    /// calls mode and link mode (the entry count is informative).
    [[nodiscard]] bool Matches(const WarmStamp& other) const noexcept {
        return size == other.size && write_time == other.write_time && driver == other.driver &&
               device_id == other.device_id && cache_version == other.cache_version &&
               no_inline == other.no_inline && link_mode == other.link_mode &&
               configuration == other.configuration;
    }
};

/// The stamp of `warm_file` as it is now, empty when its size or time cannot be read.
std::optional<WarmStamp> CurrentWarmStamp(const std::filesystem::path& warm_file, u64 entries,
                                          u32 driver, u32 device_id, u32 cache_version,
                                          u32 no_inline = 0, u32 link_mode = 0);

/// The marker of `warm_file`, empty when there is none or it cannot be read.
std::optional<WarmStamp> ReadWarmMarker(const std::filesystem::path& warm_file);

/// Writes the marker of `warm_file`. Returns false when it cannot be written.
bool WriteWarmMarker(const std::filesystem::path& warm_file, const WarmStamp& stamp);

/// True when the title's precache has never finished: no precache file yet, or no marker next
/// to it, or a recipe (FindNativeRecipe) not assembled yet. A marker that no longer matches (new
/// driver) is not seen here: the emulator checks that at boot.
bool WarmPending(u64 title_id);

/// Whether this boot precaches before the game starts: Always, or Ask when the frontend asked and
/// the user agreed (`asked`). Off never does.
[[nodiscard]] constexpr bool PrecacheBeforeStart(Settings::PrecacheShaders mode, bool asked) {
    return mode == Settings::PrecacheShaders::Always ||
           (mode == Settings::PrecacheShaders::Ask && asked);
}

/// The launch's answer to "precache shaders before the game starts" (precache_shaders = Ask):
/// set by the frontend before booting, taken (and cleared) by the renderer when it loads. Setting
/// it also clears an earlier skip request.
void RequestWarmBeforeStart(bool before_start);
bool TakeWarmBeforeStart();

/// Skip button of the loading screen: the running precache stops after its current builds and the
/// game starts. Taken (and cleared) by the renderer.
void RequestSkipWarm();
bool TakeSkipWarm();
/// Whether Skip was pressed, without clearing it (the native preparation runs before the
/// precache, and one Skip stops both).
bool SkipWarmRequested();

/// The GPU driver, device and pipeline cache version pipelines were precached for.
struct DriverStamp {
    u32 driver{};
    u32 device_id{};
    u32 cache_version{};

    bool operator==(const DriverStamp&) const = default;
};

/// The title's list of cached graphics pipelines an earlier precache built
/// (<shader dir>/<title id>/precache-graphics.bin).
std::filesystem::path PrecachedPipelinesPath(u64 title_id);

/// The pipeline key hashes in `file`, empty when there is none or it was written for another
/// driver, device or cache version.
std::unordered_set<u64> ReadPrecachedPipelines(const std::filesystem::path& file,
                                               const DriverStamp& stamp);

/// Writes `hashes` for `stamp` to `file`. Returns false when it cannot be written.
bool WritePrecachedPipelines(const std::filesystem::path& file, const DriverStamp& stamp,
                             const std::unordered_set<u64>& hashes);

/// Removes the title's precache files (compute and graphics), their markers and counts, the list
/// of precached graphics pipelines and leftover temporary files (rebuild or delete from the UI). Returns false when a file exists but cannot be removed, with
/// the reason in `error` when given.
bool DeleteWarmFiles(u64 title_id, std::string* error = nullptr);

struct Progress {
    std::string stage;   ///< "Reading game files", "Extracting shaders", "Converting", "Writing"
    size_t done{};       ///< Items done in this stage
    size_t total{};      ///< Items in this stage, 0 when unknown
};

struct Result {
    bool ok{};
    bool full_scan{};
    bool scan_finished{};       ///< Every file was visited; not a claim of every possible pipeline.
    bool native_ready{};        ///< A compatible native preparation file was assembled/verified.
    bool partial{};             ///< Missing recipes, unsupported files or incomplete coverage.
    std::string error;           ///< Empty when ok
    std::vector<std::string> warnings;
    u64 files_scanned{};
    u64 bytes_scanned{};
    u64 program_occurrences{};
    std::array<size_t, 8> stage_programs{}; ///< Nx2ShaderExtract::Stage order.
    size_t scan_errors{};
    size_t limited_files{};
    size_t recipe_programs{};
    size_t recipe_programs_missing{};
    size_t recipe_programs_failed{};
    size_t archives{};           ///< Shader archives found in the game files
    size_t compute_programs{};   ///< Entries written to the compute file
    size_t vertex_programs{};    ///< Vertex entries written to the graphics file
    size_t fragment_programs{};  ///< Fragment entries written to the graphics file
    size_t mesh_pipelines{};     ///< Speculative paired task/mesh/fragment entries
    size_t native_pipelines{};   ///< Pipelines assembled from the recipe
    size_t native_dropped{};     ///< Recipe pipelines whose programs this copy lacks
    size_t native_programs{};    ///< Native programs found in the game files
    size_t native_combinations{}; ///< Program combinations (variations) found
    size_t skipped{};            ///< Programs that failed to extract or convert
};

/// A pinned, code-free recipe attested for one title/version/executable and cache format.
struct NativeRecipeProfile {
    u64 title_id{};
    u32 game_version{};
    u32 cache_version{};
    std::string main_build_id;
    std::filesystem::path recipe_path;
    std::string expected_recipe_sha256;
};

/// Refuses a recipe whose verified provenance or game identity does not match; no files change.
std::optional<std::string> ValidateNativeRecipeProfile(
    const NativeRecipeProfile& profile, std::span<const u8> recipe, u64 actual_title_id,
    u32 actual_game_version, std::string_view actual_main_build_id);

/// Builds warm-up files for the game at `game_path` (any format the loader opens, Switch 2
/// .nup first), from the user's own copy and keys, with no external tools. Runs on the calling
/// thread; `progress` is called from it; stops early when `stop` is requested. The game's
/// existing pipeline cache, when present, is used for the draw state guesses; otherwise built-in
/// defaults are used. Mesh predictions require captured templates and verified archive pairing.
Result BuildWarmFiles(Core::System& system, const std::filesystem::path& game_path, u64 title_id,
                      const std::function<void(const Progress&)>& progress,
                      std::stop_token stop);

/// Overrides for BuildWarmFilesWith (tests and tools).
struct BuildOptions {
    /// Scan all effective RomFS files, even without a recipe. Keeps the quick path unchanged.
    bool full_scan = false;
    std::optional<NativeRecipeProfile> native_profile;
    /// Folder for the three warm-up files; empty: <shader dir>/<title id>.
    std::filesystem::path output_dir;
    /// Pipeline caches used for the draw state guesses. Unset: the title's own pipeline cache
    /// when it exists, else the built-in defaults. Set but empty: always the built-in defaults.
    std::optional<std::vector<std::filesystem::path>> runtime_caches;
    /// Recipe to assemble; empty: the one kept from an earlier build, if any.
    std::filesystem::path recipe_path;
    /// Conversion threads, 0: a quarter of the CPU threads, 2 to 6.
    size_t threads{};
    /// Bound speculative mesh combinations; 0 disables generation. Requires runtime templates.
    size_t mesh_candidates = 512;
};

/// BuildWarmFiles with explicit output folder and draw state sources.
Result BuildWarmFilesWith(Core::System& system, const std::filesystem::path& game_path,
                          u64 title_id, const BuildOptions& options,
                          const std::function<void(const Progress&)>& progress,
                          std::stop_token stop);

} // namespace VideoCommon::ShaderWarm
