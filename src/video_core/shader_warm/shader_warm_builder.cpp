// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <fmt/format.h>
#include <openssl/evp.h>

#include "native_preparation_revision.h"

#include "common/cityhash.h"
#include "common/common_funcs.h"
#include "common/hex_util.h"
#include "common/fs/fs_util.h"
#include "common/fs/file_lock.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/file_sys/nup_container.h"
#include "core/file_sys/nx2_shader_extract.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/loader/loader.h"
#include "core/loader/nso.h"
#include "video_core/pipeline_cache_files.h"
#include "video_core/nx2_recipe.h"
#include "video_core/shader_warm/prepared_store.h"
#include "video_core/shader_warm/shader_archives.h"
#include "video_core/shader_warm/mesh_warm_builder.h"
#include "video_core/shader_warm/shader_warm_builder.h"
#include "video_core/shader_warm/warm_records.h"

namespace VideoCommon::ShaderWarm {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view StageReading = "Reading game files";
constexpr std::string_view StageExtracting = "Extracting shaders";
constexpr std::string_view StageConverting = "Converting";
constexpr std::string_view StageWriting = "Writing";
/// Conversions in flight (each holds a copy of its program).
constexpr size_t MaxPendingJobs = 2048;
constexpr u32 PrecachedPipelinesMagic = 0x31504750; // "PGP1"

fs::path TitleDir(u64 title_id) {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir) /
           fmt::format("{:016x}", title_id);
}

struct ArchiveFile {
    std::string path; ///< Inside the RomFS, '/' separated (the archive identity)
    FileSys::VirtualFile file;
};

/// Appends every file under `dir` to `out`, recursively, with its '/' separated path.
void ListFiles(const FileSys::VirtualDir& dir, const std::string& prefix,
               std::vector<ArchiveFile>& out) {
    for (const auto& file : dir->GetFiles()) {
        out.push_back({prefix + file->GetName(), file});
    }
    for (const auto& sub : dir->GetSubdirectories()) {
        ListFiles(sub, prefix + sub->GetName() + "/", out);
    }
}

/// The game's RomFS trees in the order the offline pipeline read them: a .nup gives its base
/// and newest update package (both are read, like the exported packages 0 and 1); other
/// formats give the loader's RomFS.
struct GameFiles {
    std::unique_ptr<FileSys::NupContainer> nup;
    std::unique_ptr<Loader::AppLoader> loader;
    std::vector<FileSys::VirtualDir> romfs;
};

/// Opens the game file and collects its RomFS trees into `game`. Returns the error text when the
/// file cannot be opened or has no RomFS.
std::optional<std::string> OpenGame(Core::System& system, const fs::path& game_path,
                                    GameFiles& game) {
    FileSys::VirtualFilesystem filesystem = system.GetFilesystem();
    if (filesystem == nullptr) {
        filesystem = std::make_shared<FileSys::RealVfsFilesystem>();
    }
    const auto file = Core::GetGameFileFromPath(filesystem, Common::FS::PathToUTF8String(game_path));
    if (file == nullptr) {
        return "Cannot open the game file";
    }
    if (FileSys::NupContainer::IsNupFile(file)) {
        // Small memory cache: every member is read once.
        game.nup = std::make_unique<FileSys::NupContainer>(file, 128 * 1024 * 1024);
        if (game.nup->GetStatus() != Loader::ResultStatus::Success) {
            return "The game file is not a valid package";
        }
        std::vector<size_t> packages;
        if (const auto base = game.nup->FindBasePackage()) {
            packages.push_back(*base);
        }
        if (const auto update = game.nup->FindBestUpdatePackage()) {
            packages.push_back(*update);
        }
        std::sort(packages.begin(), packages.end());
        for (const size_t package : packages) {
            if (auto dir = game.nup->GetRomFSDir(package)) {
                game.romfs.push_back(std::move(dir));
            }
        }
    } else {
        game.loader = Loader::GetLoader(system, file);
        if (game.loader == nullptr) {
            return "Unsupported game file format";
        }
        FileSys::VirtualFile romfs;
        if (game.loader->ReadRomFS(romfs) != Loader::ResultStatus::Success || romfs == nullptr) {
            return "Cannot read the game's RomFS (are the keys installed?)";
        }
        if (auto dir = FileSys::ExtractRomFS(romfs)) {
            game.romfs.push_back(std::move(dir));
        }
    }
    if (game.romfs.empty()) {
        return "The game has no RomFS";
    }
    return std::nullopt;
}

struct PairHash {
    u128 a{};
    u128 b{};
    bool operator==(const PairHash&) const = default;
};
struct PairHasher {
    size_t operator()(const PairHash& h) const noexcept {
        return static_cast<size_t>(h.a[0] ^ (h.b[0] * 0x9E3779B97F4A7C15ULL));
    }
};
struct U128Hasher {
    size_t operator()(const u128& h) const noexcept {
        return static_cast<size_t>(h[0]);
    }
};

u128 Hash128(std::span<const u8> bytes) {
    return Common::CityHash128(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

/// One unique vertex or fragment program (make_graphics_warm.py: first manifest program per
/// code hash, with the origins of that program's code + control identity).
struct GraphicsProgram {
    u32 stage{};
    u128 control{};
    std::vector<u32> archives;
    bool built{};
    u64 hash{};
    u128 environment{};
};

struct Job {
    size_t seq{};
    bool graphics{};
    u32 stage{};
    size_t graphics_index{};
    std::vector<u8> program;
    std::vector<u8> constants;
    ComputeControl control{};
};

struct JobResult {
    bool ok{};
    std::vector<u8> bytes; ///< Compute: full entry. Graphics: env record.
    ComputeKey compute_key{};
    u64 hash{};
};

class Converter {
public:
    /// Starts `threads` workers that convert pushed jobs until `stop` is requested.
    Converter(size_t threads, std::stop_token stop_) : stop{std::move(stop_)} {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] { Work(); });
        }
    }

    /// Lets the workers finish the queued jobs, then joins them.
    ~Converter() {
        {
            std::scoped_lock lock{mutex};
            closing = true;
        }
        jobs_cv.notify_all();
        for (auto& worker : workers) {
            worker.join();
        }
    }

    void Push(Job job) {
        {
            std::scoped_lock lock{mutex};
            jobs.push_back(std::move(job));
            ++pushed;
        }
        jobs_cv.notify_one();
    }

    /// Next result in submission order; waits when `wait` is set, else empty when not ready.
    std::optional<JobResult> Next(bool wait) {
        std::unique_lock lock{mutex};
        for (;;) {
            const auto it = results.find(next);
            if (it != results.end()) {
                JobResult result = std::move(it->second);
                results.erase(it);
                ++next;
                return result;
            }
            if (!wait || next == pushed || stop.stop_requested()) {
                return std::nullopt;
            }
            results_cv.wait_for(lock, std::chrono::milliseconds{100});
        }
    }

    size_t Pending() {
        std::scoped_lock lock{mutex};
        return pushed - next;
    }

private:
    /// Worker loop: takes jobs in order, converts each one (skipped once stopping) and files the
    /// result under its sequence number. Returns once closing with no job left.
    void Work() {
        Common::SetCurrentThreadName("ShaderWarmBuild");
        Common::SetCurrentThreadPriority(Common::ThreadPriority::Low);
        for (;;) {
            Job job;
            {
                std::unique_lock lock{mutex};
                jobs_cv.wait(lock, [this] { return closing || !jobs.empty(); });
                if (jobs.empty()) {
                    return;
                }
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            JobResult result;
            if (!stop.stop_requested()) {
                result = Convert(job);
            }
            {
                std::scoped_lock lock{mutex};
                results.emplace(job.seq, std::move(result));
            }
            results_cv.notify_all();
        }
    }

    /// Lowers one program and makes its compute entry or graphics env record. The result is not ok
    /// when lowering or conversion failed.
    static JobResult Convert(const Job& job) {
        JobResult result;
        try {
            LoweredCode lowered;
            if (const auto error = LowerProgram(job.program, job.graphics, lowered)) {
                LOG_DEBUG(Render_Vulkan, "Shader precache: program not converted: {}", *error);
                return result;
            }
            if (job.graphics) {
                result.hash = StageHash(lowered.code);
                result.bytes = GraphicsEnv(job.stage, lowered, job.program, job.constants);
            } else {
                result.bytes =
                    ComputeEntry(lowered, job.control, job.constants, result.compute_key);
            }
            result.ok = true;
        } catch (const std::exception& e) {
            LOG_DEBUG(Render_Vulkan, "Shader precache: conversion failed: {}", e.what());
        }
        return result;
    }

    std::stop_token stop;
    std::mutex mutex;
    std::condition_variable jobs_cv;
    std::condition_variable results_cv;
    std::deque<Job> jobs;
    std::map<size_t, JobResult> results;
    size_t pushed{};
    size_t next{};
    bool closing{};
    std::vector<std::thread> workers;
};

template <typename T>
void Write(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

void WriteHeader(std::ofstream& out) {
    out.write("yuzucach", 8);
    Write(out, WarmCacheVersion);
}

std::atomic<bool> warm_before_start{false};
std::atomic<bool> skip_warm{false};

/// Records `entries` for the finished precache file at `warm_file` (see WarmCountPath).
void WriteWarmCount(const fs::path& warm_file, u64 entries) {
    std::error_code ec;
    const auto size = fs::file_size(warm_file, ec);
    if (ec) {
        return;
    }
    std::ofstream out(WarmCountPath(warm_file), std::ios::trunc);
    out << entries << ' ' << size << '\n';
}

} // Anonymous namespace

fs::path NativeRecipePath(u64 title_id) {
    return TitleDir(title_id) / "native.nx2recipe";
}

fs::path NativePreparationPath(u64 title_id) {
    return TitleDir(title_id) / "native-preparation.bin";
}

fs::path FindNativeRecipe(u64 title_id) {
    std::error_code ec;
    if (fs::exists(NativeRecipePath(title_id), ec)) {
        return NativeRecipePath(title_id);
    }
    fs::path recipe_dir;
#ifdef _WIN32
    recipe_dir = Common::FS::GetExeDirectory() / "recipes";
#elif defined(__linux__)
    const auto executable = fs::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return {};
    }
    recipe_dir = executable.parent_path() / "recipes";
#elif defined(__APPLE__)
    recipe_dir = Common::FS::GetBundleDirectory() / "Contents/MacOS/recipes";
#else
    return {};
#endif
    std::vector<fs::path> candidates;
    for (const auto& entry : fs::directory_iterator(recipe_dir, ec)) {
        if (entry.path().extension() == ".nx2recipe" && entry.is_regular_file(ec)) {
            candidates.push_back(entry.path());
        }
    }
    std::ranges::sort(candidates);
    for (const auto& path : candidates) {
        const auto size = fs::file_size(path, ec);
        if (ec || size > 64 * 1024 * 1024) {
            continue;
        }
        std::vector<u8> bytes(size);
        std::ifstream file(path, std::ios::binary);
        if (!file.read(reinterpret_cast<char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()))) {
            continue;
        }
        const auto header = Nx2Recipe::ReadHeader(bytes);
        if (header && header->title_id == title_id && header->cache_version == WarmCacheVersion) {
            return path;
        }
    }
    return {};
}

fs::path ComputeWarmPath(u64 title_id) {
    return TitleDir(title_id) / "warm-compute.bin";
}

fs::path GraphicsWarmPath(u64 title_id) {
    return TitleDir(title_id) / "warm-graphics.bin";
}

fs::path MeshWarmPath(u64 title_id) {
    return TitleDir(title_id) / "warm-mesh.bin";
}

fs::path WarmMarkerPath(const fs::path& warm_file) {
    fs::path marker = warm_file;
    marker.replace_extension(".done");
    return marker;
}

fs::path WarmCountPath(const fs::path& warm_file) {
    fs::path count = warm_file;
    count.replace_extension(".count");
    return count;
}

u64 ReadWarmCount(const fs::path& warm_file) {
    std::ifstream in(WarmCountPath(warm_file));
    u64 entries{};
    u64 size{};
    if (!(in >> entries >> size)) {
        return 0;
    }
    std::error_code ec;
    const auto actual = fs::file_size(warm_file, ec);
    return !ec && actual == size ? entries : 0;
}

std::optional<WarmStamp> CurrentWarmStamp(const fs::path& warm_file, u64 entries, u32 driver,
                                          u32 device_id, u32 cache_version, u32 no_inline,
                                          u32 link_mode) {
    std::error_code ec;
    const auto size = fs::file_size(warm_file, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto time = fs::last_write_time(warm_file, ec);
    if (ec) {
        return std::nullopt;
    }
    return WarmStamp{size,      static_cast<s64>(time.time_since_epoch().count()),
                     entries,   driver,
                     device_id, cache_version,
                     no_inline, link_mode};
}

std::optional<WarmStamp> ReadWarmMarker(const fs::path& warm_file) {
    std::ifstream in(WarmMarkerPath(warm_file));
    WarmStamp stamp;
    if (!(in >> stamp.size >> stamp.write_time >> stamp.entries >> stamp.driver >>
          stamp.device_id >> stamp.cache_version >> stamp.no_inline >> stamp.link_mode)) {
        return std::nullopt;
    }
    if (!(in >> stamp.configuration)) {
        stamp.configuration.clear();
    }
    return stamp;
}

bool WriteWarmMarker(const fs::path& warm_file, const WarmStamp& stamp) {
    std::ofstream out(WarmMarkerPath(warm_file), std::ios::trunc);
    out << stamp.size << ' ' << stamp.write_time << ' ' << stamp.entries << ' ' << stamp.driver
        << ' ' << stamp.device_id << ' ' << stamp.cache_version << ' ' << stamp.no_inline << ' '
        << stamp.link_mode << ' ' << stamp.configuration << '\n';
    out.close();
    return static_cast<bool>(out);
}

bool WarmPending(u64 title_id) {
    const auto file = ComputeWarmPath(title_id);
    std::error_code ec;
    if (!fs::exists(file, ec) || !fs::exists(WarmMarkerPath(file), ec)) {
        return true;
    }
    for (const auto& extra : {GraphicsWarmPath(title_id), MeshWarmPath(title_id)}) {
        if (extra == MeshWarmPath(title_id) &&
            !Settings::values.use_mesh_shader_warmup.GetValue()) {
            continue;
        }
        if (fs::exists(extra, ec) && fs::file_size(extra, ec) > 12 && !ec &&
            !fs::exists(WarmMarkerPath(extra), ec)) {
            return true;
        }
    }
    return !fs::exists(NativePreparationPath(title_id), ec) && !FindNativeRecipe(title_id).empty();
}

void RequestWarmBeforeStart(bool before_start) {
    skip_warm.store(false, std::memory_order_relaxed);
    warm_before_start.store(before_start, std::memory_order_relaxed);
}

bool TakeWarmBeforeStart() {
    return warm_before_start.exchange(false, std::memory_order_relaxed);
}

void RequestSkipWarm() {
    skip_warm.store(true, std::memory_order_relaxed);
}

bool SkipWarmRequested() {
    return skip_warm.load(std::memory_order_relaxed);
}

bool TakeSkipWarm() {
    return skip_warm.exchange(false, std::memory_order_relaxed);
}

fs::path PrecachedPipelinesPath(u64 title_id) {
    return TitleDir(title_id) / "precache-graphics.bin";
}

std::unordered_set<u64> ReadPrecachedPipelines(const fs::path& file, const DriverStamp& stamp) {
    std::unordered_set<u64> hashes;
    std::ifstream in(file, std::ios::binary);
    u32 magic{};
    DriverStamp saved;
    u64 count{};
    if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic)) ||
        magic != PrecachedPipelinesMagic ||
        !in.read(reinterpret_cast<char*>(&saved.driver), sizeof(saved.driver)) ||
        !in.read(reinterpret_cast<char*>(&saved.device_id), sizeof(saved.device_id)) ||
        !in.read(reinterpret_cast<char*>(&saved.cache_version), sizeof(saved.cache_version)) ||
        !in.read(reinterpret_cast<char*>(&count), sizeof(count)) || saved != stamp) {
        return hashes;
    }
    for (u64 i = 0; i < count; ++i) {
        u64 hash{};
        if (!in.read(reinterpret_cast<char*>(&hash), sizeof(hash))) {
            break;
        }
        hashes.insert(hash);
    }
    return hashes;
}

bool WritePrecachedPipelines(const fs::path& file, const DriverStamp& stamp,
                             const std::unordered_set<u64>& hashes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    Write(out, PrecachedPipelinesMagic);
    Write(out, stamp.driver);
    Write(out, stamp.device_id);
    Write(out, stamp.cache_version);
    Write(out, static_cast<u64>(hashes.size()));
    for (const u64 hash : hashes) {
        Write(out, hash);
    }
    out.close();
    return static_cast<bool>(out);
}

bool DeleteWarmFiles(u64 title_id, std::string* error) {
    std::error_code status_error;
    const bool exists = fs::exists(TitleDir(title_id), status_error);
    if (status_error) {
        if (error) *error = status_error.message();
        return false;
    }
    if (!exists) return true;
    Common::FS::FileLock lock{TitleDir(title_id) / "native-preparation.lock"};
    if (!lock.Held()) {
        if (error) *error = "This game's shader cache is in use by another instance";
        return false;
    }
    bool ok = true;
    const auto remove = [&](const fs::path& path) {
        std::error_code ec;
        fs::remove(path, ec);
        if (ec) {
            ok = false;
            if (error != nullptr) {
                *error = ec.message();
            }
        }
    };
    for (const auto& warm : {ComputeWarmPath(title_id), GraphicsWarmPath(title_id),
                             MeshWarmPath(title_id)}) {
        fs::path tmp = warm;
        tmp += ".tmp";
        for (const auto& path : {warm, WarmMarkerPath(warm), WarmCountPath(warm), tmp}) {
            remove(path);
        }
    }
    remove(PrecachedPipelinesPath(title_id));
    for (const auto& path : {NativeRecipePath(title_id), NativePreparationPath(title_id),
                             TitleDir(title_id) / "native-prepared.journal",
                             TitleDir(title_id) / "native-driver-cache.txt",
                             TitleDir(title_id) / "native-source.txt"}) {
        remove(path);
        remove(fs::path{path}.concat(".tmp"));
    }
    return ok;
}

namespace {

constexpr std::string_view StageNativeChecking = "Checking game contents";
constexpr std::string_view StageNativeExtracting = "Extracting native shaders";
constexpr std::string_view StageNativeAssembling = "Assembling pipelines from recipe";
constexpr std::string_view StageNativeWriting = "Writing native preparation";

/// The RomFS the game sees: base, update and LayeredFS mods applied.
FileSys::VirtualDir OpenEffectiveRomFS(Core::System& system, const fs::path& game_path,
                                       u64 title_id, std::string& error) {
    FileSys::VirtualFilesystem filesystem = system.GetFilesystem();
    if (filesystem == nullptr) {
        filesystem = std::make_shared<FileSys::RealVfsFilesystem>();
    }
    const auto file = Core::GetGameFileFromPath(filesystem, Common::FS::PathToUTF8String(game_path));
    auto loader = file ? Loader::GetLoader(system, file) : nullptr;
    u64 program_id{};
    if (!loader || loader->ReadProgramId(program_id) != Loader::ResultStatus::Success ||
        program_id != title_id) {
        error = "The selected game file does not match this title";
        return nullptr;
    }
    FileSys::RomFSFactory factory(*loader, system.GetContentProvider(),
                                  system.GetFileSystemController());
    FileSys::VirtualFile update;
    loader->ReadUpdateRaw(update);
    factory.SetPackedUpdate(update);
    auto root = FileSys::ExtractRomFS(factory.OpenCurrentProcess(title_id));
    if (!root) {
        error = "Cannot read the game's files (are the keys installed?)";
    }
    return root;
}

std::string Sha256(std::span<const u8> bytes) {
    std::array<u8, 32> digest{};
    unsigned length{};
    if (EVP_Digest(bytes.data(), bytes.size(), digest.data(), &length, EVP_sha256(), nullptr) != 1 ||
        length != digest.size()) {
        return {};
    }
    std::string result;
    for (const u8 byte : digest) {
        result += fmt::format("{:02x}", byte);
    }
    return result;
}

struct NativeGameIdentity {
    std::optional<u32> version;
    std::string build_id;
};

NativeGameIdentity ReadNativeIdentity(Core::System& system, const fs::path& path, u64 title_id) {
    auto filesystem = system.GetFilesystem();
    if (!filesystem) {
        filesystem = std::make_shared<FileSys::RealVfsFilesystem>();
    }
    const auto file = Core::GetGameFileFromPath(filesystem, Common::FS::PathToUTF8String(path));
    FileSys::VirtualFile main;
    NativeGameIdentity result;
    if (file && FileSys::NupContainer::IsNupFile(file)) {
        FileSys::NupContainer nup{file, 16 * 1024 * 1024};
        if (nup.GetStatus() != Loader::ResultStatus::Success) {
            return result;
        }
        auto selected = nup.FindBestUpdatePackage();
        if (!selected || !nup.GetExeFSDir(*selected)) {
            selected = nup.FindBasePackage();
        }
        if (!selected) {
            return result;
        }
        result.version = nup.GetPackages()[*selected].version;
        auto exefs = nup.GetExeFSDir(*selected);
        const FileSys::PatchManager patches{title_id, system.GetFileSystemController(),
                                            system.GetContentProvider()};
        exefs = patches.PatchExeFS(std::move(exefs));
        main = exefs ? exefs->GetFile("main") : nullptr;
    } else if (file && file->GetName() == "main") {
        main = file;
    }
    Loader::NSOHeader header{};
    if (main && main->ReadObject(&header) == sizeof(header) &&
        header.magic == Common::MakeMagic('N', 'S', 'O', '0')) {
        result.build_id = Common::HexToString(header.build_id);
        result.build_id.resize(result.build_id.find_last_not_of('0') + 1);
        // A loose dump carries no package-version field. This supported executable's identity
        // independently identifies the same OOT update as the pinned recipe.
        if (!result.version && result.build_id ==
            "E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348") {
            result.version = 131072;
        }
    }
    return result;
}

Result BuildFullNativeFiles(Core::System& system, const fs::path& game_path, u64 title_id,
                            const BuildOptions& options,
                            const std::function<void(const Progress&)>& progress,
                            std::stop_token stop) {
    namespace X = FileSys::Nx2ShaderExtract;
    Result result;
    result.full_scan = true;
    const auto report = [&](std::string_view stage, size_t done = 0, size_t total = 0) {
        if (progress) {
            progress({std::string(stage), done, total});
        }
    };
    const auto fail = [&](std::string error) {
        result.ok = false;
        result.partial = true;
        result.error = std::move(error);
        return result;
    };
    try {
        report(StageNativeChecking);
        std::string error;
        const auto root = OpenEffectiveRomFS(system, game_path, title_id, error);
        if (!root) {
            return fail(error);
        }
        report(StageNativeExtracting);
        const std::array roots{root};
        X::Progress scanned;
        // One archive is inflated at a time; retained programs have a separate hard budget.
        // Reads are serialized even for encrypted NCA views, which share mutable read caches.
        auto extraction = std::async(std::launch::async, [&] {
            return X::Extract(roots, 1, &scanned, stop, true,
                              // OOT's model shader archive inflates to 320,672,184 bytes.
                              {.max_blob_bytes = size_t{512} << 20,
                               .max_retained_bytes = size_t{1} << 30,
                               .inspect_all_compressed = true});
        });
        while (extraction.wait_for(std::chrono::milliseconds{100}) != std::future_status::ready) {
            report(StageNativeExtracting, scanned.files_done.load(), scanned.files_total.load());
        }
        const auto extracted = extraction.get();
        result.files_scanned = extracted.stats.files;
        result.bytes_scanned = extracted.stats.file_bytes;
        result.program_occurrences = extracted.stats.occurrences;
        result.native_programs = extracted.programs.size();
        result.native_combinations = extracted.variations.size();
        result.scan_errors = extracted.errors.size() + extracted.stats.embedded_failed;
        result.limited_files = extracted.stats.limited_files;
        for (const auto& program : extracted.programs) {
            ++result.stage_programs[static_cast<size_t>(program.stage)];
        }
        result.scan_finished = !stop.stop_requested();
        result.partial = result.scan_errors != 0 || result.limited_files != 0 ||
                         extracted.stats.programs_limited != 0;
        for (size_t i = 0; i < std::min<size_t>(extracted.errors.size(), 16); ++i) {
            result.warnings.push_back(extracted.errors[i]);
        }
        if (extracted.stats.embedded_failed != 0) {
            result.warnings.push_back(fmt::format("{} embedded compressed candidates could not "
                                                  "be decoded", extracted.stats.embedded_failed));
        }
        report(StageNativeExtracting, scanned.files_done.load(), scanned.files_total.load());
        if (stop.stop_requested()) {
            return fail("Stopped");
        }
        if (!options.native_profile) {
            result.ok = true;
            result.partial = true;
            result.warnings.push_back("No verified recipe profile is available; programs were "
                                      "scanned, but unseen graphics pipelines were not prepared");
            return result;
        }
        const auto& profile = *options.native_profile;
        const auto recipe_path = options.recipe_path.empty() ? profile.recipe_path
                                                              : options.recipe_path;
        std::error_code ec;
        const auto recipe_size = fs::file_size(recipe_path, ec);
        if (ec || recipe_size > 64 * 1024 * 1024) {
            return fail("Cannot read the verified recipe, or it exceeds 64 MiB");
        }
        std::vector<u8> recipe(recipe_size);
        std::ifstream input(recipe_path, std::ios::binary);
        if (!input.read(reinterpret_cast<char*>(recipe.data()), recipe.size())) {
            return fail("Cannot read the verified recipe");
        }
        const auto identity = ReadNativeIdentity(system, game_path, title_id);
        if (!identity.version) {
            return fail("Cannot verify this container's raw game version and executable build");
        }
        if (const auto invalid = ValidateNativeRecipeProfile(
                profile, recipe, title_id, *identity.version, identity.build_id)) {
            return fail(*invalid);
        }
        report(StageNativeAssembling);
        Nx2Recipe::Report assembled_report;
        const unsigned threads = static_cast<unsigned>(std::clamp<size_t>(
            options.threads != 0 ? options.threads : 2, 1, 4));
        const auto assembled = Nx2Recipe::Assemble(recipe, extracted, WarmCacheVersion,
                                                  assembled_report, threads, stop);
        result.recipe_programs = assembled_report.programs;
        result.recipe_programs_missing = assembled_report.programs_missing;
        result.recipe_programs_failed = assembled_report.programs_failed;
        result.native_pipelines = assembled_report.records;
        result.native_dropped = assembled_report.records_dropped;
        result.partial |= result.native_dropped != 0 || result.recipe_programs_missing != 0 ||
                          result.recipe_programs_failed != 0 ||
                          result.native_programs > result.recipe_programs;
        result.warnings.push_back("Pipeline states are recorded or predicted; new runtime states "
                                  "can still require compilation");
        if (stop.stop_requested()) {
            return fail("Stopped");
        }
        if (assembled.empty() || assembled_report.records == 0) {
            return fail("No recipe pipelines match the programs in this copy of the game");
        }
        report(StageNativeWriting);
        const auto dir = options.output_dir.empty() ? TitleDir(title_id) : options.output_dir;
        fs::create_directories(dir);
        if (!AtomicWrite(dir / "native-preparation.bin", assembled) ||
            !AtomicWrite(dir / "native.nx2recipe", recipe)) {
            return fail("Cannot publish the native preparation files");
        }
        result.native_ready = true;
        result.ok = true;
        report(StageNativeWriting, 1, 1);
        return result;
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}

/// SHA-256 over every file's path, size and contents, in path order. Timestamps are not used.
std::optional<std::string> HashTree(const FileSys::VirtualDir& root,
                                    const std::function<void(size_t, size_t)>& progress,
                                    std::stop_token stop) {
    std::vector<ArchiveFile> files;
    ListFiles(root, "", files);
    std::ranges::sort(files, {}, &ArchiveFile::path);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> hash(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!hash || EVP_DigestInit_ex(hash.get(), EVP_sha256(), nullptr) != 1) {
        return std::nullopt;
    }
    std::vector<u8> buffer(1 << 20);
    for (size_t i = 0; i < files.size(); ++i) {
        progress(i, files.size());
        const auto& item = files[i];
        const u64 length = item.file->GetSize();
        const u64 path_length = item.path.size();
        EVP_DigestUpdate(hash.get(), &path_length, sizeof(path_length));
        EVP_DigestUpdate(hash.get(), item.path.data(), item.path.size());
        EVP_DigestUpdate(hash.get(), &length, sizeof(length));
        for (u64 offset = 0; offset < length;) {
            if (stop.stop_requested()) {
                return std::nullopt;
            }
            const size_t wanted =
                static_cast<size_t>(std::min<u64>(buffer.size(), length - offset));
            if (item.file->Read(buffer.data(), wanted, offset) != wanted ||
                EVP_DigestUpdate(hash.get(), buffer.data(), wanted) != 1) {
                return std::nullopt;
            }
            offset += wanted;
        }
    }
    std::array<u8, 32> digest{};
    unsigned digest_size{};
    if (EVP_DigestFinal_ex(hash.get(), digest.data(), &digest_size) != 1) {
        return std::nullopt;
    }
    std::string result;
    for (const u8 byte : digest) {
        result += fmt::format("{:02x}", byte);
    }
    return result;
}

/// Turns the title's recipe into native-preparation.bin with the player's own game files. The
/// Vulkan pipelines are built from it on the loading screen (PipelineCache::PreparePrecompiled).
Result BuildNativeFiles(Core::System& system, const fs::path& game_path, u64 title_id,
                        const BuildOptions& options,
                        const std::function<void(const Progress&)>& progress,
                        std::stop_token stop) {
    Result result;
    const auto report = [&](std::string_view stage, size_t done = 0, size_t total = 0) {
        if (progress) {
            progress(Progress{std::string(stage), done, total});
        }
    };
    const auto fail = [&](std::string error) {
        result.ok = false;
        result.error = std::move(error);
        LOG_WARNING(Render_Vulkan, "Native shader preparation for {:016x}: {}", title_id,
                    result.error);
        return result;
    };
    const auto dir = options.output_dir.empty() ? TitleDir(title_id) : options.output_dir;
    const auto stored_recipe = dir / "native.nx2recipe";
    const auto recipe_path = options.recipe_path.empty() ? stored_recipe : options.recipe_path;
    try {
        std::error_code ec;
        fs::create_directories(dir, ec);
        const auto size = fs::file_size(recipe_path, ec);
        if (ec || size > 64 * 1024 * 1024) {
            return fail("Cannot read the recipe, or it is larger than 64 MiB");
        }
        std::vector<u8> recipe(size);
        std::ifstream input(recipe_path, std::ios::binary);
        if (!input.read(reinterpret_cast<char*>(recipe.data()),
                        static_cast<std::streamsize>(recipe.size()))) {
            return fail("Cannot read the recipe");
        }
        const auto header = Nx2Recipe::ReadHeader(recipe);
        if (!header) {
            return fail("Not a Lichii-tgm precompile recipe; caches from other emulators are not "
                        "supported");
        }
        if (header->title_id != title_id) {
            return fail(fmt::format("The recipe is for title {:016X}", header->title_id));
        }
        if (header->cache_version != WarmCacheVersion) {
            return fail(fmt::format("The recipe is for pipeline cache version {}; this build "
                                    "uses {}",
                                    header->cache_version, WarmCacheVersion));
        }

        report(StageNativeChecking);
        std::string error;
        const auto root = OpenEffectiveRomFS(system, game_path, title_id, error);
        if (!root) {
            return fail(error);
        }
        const auto tree = HashTree(
            root, [&](size_t done, size_t total) { report(StageNativeChecking, done, total); },
            stop);
        if (stop.stop_requested()) {
            return fail("Stopped");
        }
        if (!tree) {
            return fail("Cannot read the game's files");
        }
        // Everything the assembled file depends on. The Vulkan journal has its own stamp.
        const std::string stamp =
            fmt::format("{:016x} {} {} {} {}", title_id, WarmCacheVersion,
                        LICHII_PREPARATION_REVISION, *tree, ContentHash(recipe_path));
        const auto source = dir / "native-source.txt";
        const auto output = dir / "native-preparation.bin";
        {
            std::ifstream previous(source, std::ios::binary);
            std::string old_stamp, old_content;
            std::getline(previous, old_stamp);
            std::getline(previous, old_content);
            if (old_stamp == stamp && !old_content.empty() && ContentHash(output) == old_content) {
                std::ifstream prepared(output, std::ios::binary);
                u32 count{};
                prepared.seekg(16);
                prepared.read(reinterpret_cast<char*>(&count), sizeof(count));
                result.ok = true;
                result.native_pipelines = count;
                report(StageNativeWriting, 1, 1);
                return result;
            }
        }

        report(StageNativeExtracting);
        const std::array roots{root};
        FileSys::Nx2ShaderExtract::Progress extract_progress;
        const auto extracted =
            FileSys::Nx2ShaderExtract::Extract(roots, 0, &extract_progress, stop);
        if (stop.stop_requested()) {
            return fail("Stopped");
        }
        result.native_programs = extracted.programs.size();
        result.native_combinations = extracted.variations.size();

        report(StageNativeAssembling);
        const unsigned threads = static_cast<unsigned>(
            options.threads != 0
                ? options.threads
                : std::clamp<size_t>(std::thread::hardware_concurrency() / 2, 2, 12));
        Nx2Recipe::Report assembled_report;
        const auto assembled = Nx2Recipe::Assemble(recipe, extracted, WarmCacheVersion,
                                                   assembled_report, threads, stop);
        if (stop.stop_requested()) {
            return fail("Stopped");
        }
        result.native_pipelines = assembled_report.records;
        result.native_dropped = assembled_report.records_dropped;
        LOG_INFO(Render_Vulkan,
                 "Native shader preparation for {:016x}: {} programs, {} combinations found; "
                 "{} of the recipe's {} programs missing, {} not lowered; {} pipelines, {} "
                 "dropped",
                 title_id, extracted.programs.size(), extracted.variations.size(),
                 assembled_report.programs_missing, assembled_report.programs,
                 assembled_report.programs_failed, assembled_report.records,
                 assembled_report.records_dropped);
        if (assembled.empty() || assembled_report.records == 0) {
            return fail("None of the recipe's pipelines match this copy of the game (another "
                        "game version?)");
        }

        report(StageNativeWriting, 0, 1);
        if (!AtomicWrite(output, assembled) ||
            (recipe_path != stored_recipe && !AtomicWrite(stored_recipe, recipe))) {
            return fail("Cannot write to " + Common::FS::PathToUTF8String(dir));
        }
        const std::string source_text = stamp + "\n" + ContentHash(output) + "\n";
        if (!AtomicWrite(source, std::span(reinterpret_cast<const u8*>(source_text.data()),
                                           source_text.size()))) {
            return fail("Cannot write to " + Common::FS::PathToUTF8String(dir));
        }
        report(StageNativeWriting, 1, 1);
        result.ok = true;
        return result;
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}

} // Anonymous namespace

std::optional<std::string> ValidateNativeRecipeProfile(
    const NativeRecipeProfile& profile, std::span<const u8> recipe, u64 actual_title_id,
    u32 actual_game_version, std::string_view actual_main_build_id) {
    if (profile.expected_recipe_sha256.empty() || Sha256(recipe) != profile.expected_recipe_sha256) {
        return "The recipe SHA-256 does not match the verified profile";
    }
    const auto header = Nx2Recipe::ReadHeader(recipe);
    if (!header || header->title_id != actual_title_id || profile.title_id != actual_title_id ||
        header->cache_version != WarmCacheVersion || profile.cache_version != WarmCacheVersion ||
        header->game_version != profile.game_version || actual_game_version != profile.game_version ||
        actual_main_build_id.empty() || actual_main_build_id != profile.main_build_id) {
        return "The recipe does not match this game's verified version, executable build and "
               "pipeline cache format";
    }
    return std::nullopt;
}

Result BuildWarmFiles(Core::System& system, const fs::path& game_path, u64 title_id,
                      const std::function<void(const Progress&)>& progress,
                      std::stop_token stop) {
    return BuildWarmFilesWith(system, game_path, title_id, BuildOptions{}, progress,
                              std::move(stop));
}

namespace {
Result BuildComputeWarmFiles(Core::System& system, const fs::path& game_path, u64 title_id,
                             const BuildOptions& options,
                             const std::function<void(const Progress&)>& progress,
                             std::stop_token stop) {
    Result result;
    const auto report = [&](std::string_view stage, size_t done, size_t total) {
        if (progress) {
            progress(Progress{std::string(stage), done, total});
        }
    };
    const auto begin = std::chrono::steady_clock::now();
    report(StageReading, 0, 0);

    const fs::path output_dir = options.output_dir.empty() ? TitleDir(title_id) : options.output_dir;
    std::error_code ec;
    fs::create_directories(output_dir, ec);
    const fs::path compute_path = output_dir / "warm-compute.bin";
    const fs::path graphics_path = output_dir / "warm-graphics.bin";
    const fs::path mesh_path = output_dir / "warm-mesh.bin";
    fs::path mesh_tmp = mesh_path;
    mesh_tmp += ".tmp";
    fs::path compute_tmp = compute_path;
    compute_tmp += ".tmp";
    fs::path graphics_tmp = graphics_path;
    graphics_tmp += ".tmp";
    std::ofstream compute_out;
    std::ofstream graphics_out;
    std::ofstream mesh_out;
    const auto fail = [&](std::string error) {
        compute_out.close();
        graphics_out.close();
        mesh_out.close();
        std::error_code ignored;
        fs::remove(compute_tmp, ignored);
        fs::remove(graphics_tmp, ignored);
        fs::remove(mesh_tmp, ignored);
        result.ok = false;
        result.error = std::move(error);
        LOG_WARNING(Render_Vulkan, "Shader precache build for {:016x} failed: {}", title_id,
                    result.error);
        return result;
    };

    // Draw state guesses.
    std::vector<fs::path> caches;
    if (options.runtime_caches) {
        caches = *options.runtime_caches;
    } else {
        // The current version's cache, or right after a version bump, the newest older cache
        // with the same record layout (the play history the guesses come from).
        const auto own = VideoCommon::VersionedPipelineCachePath(
            TitleDir(title_id), "vulkan", VideoCommon::VulkanPipelineCacheVersion);
        if (fs::exists(own, ec)) {
            caches.push_back(own);
        } else {
            for (const u32 version : CompatibleRuntimeCacheVersions) {
                const auto older =
                    VideoCommon::VersionedPipelineCachePath(TitleDir(title_id), "vulkan", version);
                if (fs::exists(older, ec)) {
                    caches.push_back(older);
                    break;
                }
            }
        }
    }
    const RuntimeStats stats = ReadRuntimeStats(caches);
    MeshWarmRecipes mesh_recipes{caches, options.mesh_candidates};
    const bool use_stats = stats.occurrences != 0;
    const GraphicsKey vertex_template = stats.templates[0] ? *stats.templates[0]
                                                           : DefaultTemplate(true);
    const GraphicsKey fragment_template = stats.templates[1] ? *stats.templates[1]
                                                             : DefaultTemplate(false);
    LOG_INFO(Render_Vulkan,
             "Shader precache build for {:016x}: {} runtime stage records from {} cache(s){}",
             title_id, stats.occurrences, caches.size(),
             use_stats ? "" : ", built-in draw state defaults");

    GameFiles game;
    if (const auto error = OpenGame(system, game_path, game)) {
        return fail(*error);
    }

    // Archive list, in the offline pipeline's order: every shader archive of each package, then
    // each package's effects, sorted by path inside a package (see ClassifyGameFile).
    std::vector<ArchiveFile> archives;
    std::vector<ArchiveFile> effects;
    ZstdDictionaries dictionaries;
    FileSys::VirtualFile dictionary_pack;
    for (const auto& romfs : game.romfs) {
        std::vector<ArchiveFile> files;
        ListFiles(romfs, "", files);
        std::sort(files.begin(), files.end(),
                  [](const ArchiveFile& a, const ArchiveFile& b) { return a.path < b.path; });
        std::vector<ArchiveFile> package_effects;
        for (auto& file : files) {
            switch (ClassifyGameFile(file.path)) {
            case GameFileKind::ShaderArchive:
                archives.push_back(file);
                break;
            case GameFileKind::Effect:
                package_effects.push_back(file);
                break;
            case GameFileKind::ZstdDictionaries:
                dictionary_pack = file.file; // the newest package wins
                break;
            case GameFileKind::Other:
                break;
            }
        }
        effects.insert(effects.end(), package_effects.begin(), package_effects.end());
    }
    archives.insert(archives.end(), effects.begin(), effects.end());
    if (dictionary_pack) {
        try {
            dictionaries.Load(dictionary_pack->ReadAllBytes());
        } catch (const ArchiveError& e) {
            LOG_WARNING(Render_Vulkan, "Shader precache: zstd dictionary pack: {}", e.what());
        }
    }
    // Without archives the files are written empty: the precache then has only the pipeline
    // cache of earlier sessions to build.
    result.archives = archives.size();
    LOG_INFO(Render_Vulkan, "Shader precache build: {} archives, {} zstd dictionaries",
             archives.size(), dictionaries.Count());

    compute_out.open(compute_tmp, std::ios::binary | std::ios::trunc);
    graphics_out.open(graphics_tmp, std::ios::binary | std::ios::trunc);
    mesh_out.open(mesh_tmp, std::ios::binary | std::ios::trunc);
    if (!compute_out || !graphics_out || !mesh_out) {
        return fail("Cannot write to " + Common::FS::PathToUTF8String(output_dir));
    }
    WriteHeader(compute_out);
    WriteHeader(graphics_out);
    WriteHeader(mesh_out);

    // Bookkeeping in program order (the order the manifest lists programs).
    std::unordered_set<PairHash, PairHasher> compute_identities;
    std::unordered_map<u128, size_t, U128Hasher> graphics_index; // code -> program
    std::vector<GraphicsProgram> graphics;
    std::unordered_set<u128, U128Hasher> compute_keys;
    std::unordered_set<u128, U128Hasher> fragment_hashes;
    std::unordered_set<u128, U128Hasher> vertex_hashes;
    struct WrittenVertex {
        size_t program;
        u64 key_offset;
    };
    std::vector<WrittenVertex> written_vertices;
    struct HeldVertex {
        size_t program;
        std::vector<u8> env;
    };
    std::vector<HeldVertex> held_vertices;
    size_t archive_errors = 0;
    size_t member_errors = 0;
    size_t seq = 0;

    const size_t threads =
        options.threads != 0
            ? options.threads
            : std::clamp<size_t>(std::thread::hardware_concurrency() / 4, 2, 6);
    auto converter = std::make_unique<Converter>(threads, stop);

    // Consumes finished conversions in submission order (first program wins on equal keys).
    std::vector<std::pair<bool, size_t>> job_kinds; // graphics?, graphics program index
    size_t consumed = 0;
    const auto consume = [&](JobResult&& r) {
        const auto [is_graphics, index] = job_kinds[consumed++];
        if (!r.ok) {
            ++result.skipped;
            return;
        }
        if (!is_graphics) {
            const u128 key = Hash128(r.bytes);
            if (compute_keys.insert(key).second) {
                compute_out.write(reinterpret_cast<const char*>(r.bytes.data()),
                                  static_cast<std::streamsize>(r.bytes.size()));
                ++result.compute_programs;
            }
            return;
        }
        GraphicsProgram& program = graphics[index];
        program.built = true;
        program.hash = r.hash;
        program.environment = Hash128(r.bytes);
        if (program.stage == 4) {
            if (!fragment_hashes.insert(program.environment).second) {
                return; // same key as an earlier entry
            }
            const auto key = GraphicsEntryKey(4, fragment_template, r.hash, 0);
            Write(graphics_out, u32{1});
            graphics_out.write(reinterpret_cast<const char*>(r.bytes.data()),
                               static_cast<std::streamsize>(r.bytes.size()));
            graphics_out.write(reinterpret_cast<const char*>(key.data()),
                               static_cast<std::streamsize>(key.size()));
            ++result.fragment_programs;
            return;
        }
        if (!vertex_hashes.insert(program.environment).second) {
            // Same stage hash: same key only if the attribute guess matches, known at the end.
            held_vertices.push_back({index, std::move(r.bytes)});
            return;
        }
        const auto key = GraphicsEntryKey(0, vertex_template, r.hash, 0);
        Write(graphics_out, u32{1});
        graphics_out.write(reinterpret_cast<const char*>(r.bytes.data()),
                           static_cast<std::streamsize>(r.bytes.size()));
        const auto key_offset = static_cast<u64>(graphics_out.tellp());
        graphics_out.write(reinterpret_cast<const char*>(key.data()),
                           static_cast<std::streamsize>(key.size()));
        written_vertices.push_back({index, key_offset});
    };
    const auto drain = [&](bool wait_all) {
        while (auto r = converter->Next(wait_all)) {
            consume(std::move(*r));
        }
    };

    for (size_t archive_index = 0; archive_index < archives.size(); ++archive_index) {
        if (stop.stop_requested()) {
            converter.reset();
            return fail("Stopped");
        }
        report(StageExtracting, archive_index, archives.size());
        const auto& archive = archives[archive_index];
        const auto data = archive.file->ReadAllBytes();
        const auto name = archive.path.substr(archive.path.rfind('/') + 1);
        const auto on_record = [&](const ShaderRecord& record) {
            const bool is_graphics =
                record.stage == ProgramStage::Vertex || record.stage == ProgramStage::Fragment;
            if (record.stage != ProgramStage::Compute && !is_graphics) {
                return;
            }
            const std::array program_parts{Hash128(record.code), Hash128(record.constants)};
            const u128 code = Common::CityHash128(
                reinterpret_cast<const char*>(program_parts.data()), sizeof(program_parts));
            const u128 control = Hash128(record.control);
            Job job;
            if (is_graphics) {
                const auto [it, inserted] = graphics_index.try_emplace(code, graphics.size());
                if (!inserted) {
                    GraphicsProgram& program = graphics[it->second];
                    if (program.control == control) {
                        program.archives.push_back(static_cast<u32>(archive_index));
                    }
                    return;
                }
                graphics.push_back({record.stage == ProgramStage::Vertex ? 0U : 4U, control,
                                    {static_cast<u32>(archive_index)}});
                job.graphics = true;
                job.stage = graphics.back().stage;
                job.graphics_index = it->second;
            } else {
                if (!compute_identities.insert(PairHash{code, control}).second) {
                    return;
                }
                const auto values = ReadComputeControl(record.control);
                if (!values || !values->workgroup[0] || !values->workgroup[1] ||
                    !values->workgroup[2]) {
                    ++result.skipped;
                    return;
                }
                job.control = *values;
                job.stage = 5;
            }
            job.seq = seq++;
            job.program.assign(record.code.begin(), record.code.end());
            job.constants.assign(record.constants.begin(), record.constants.end());
            job_kinds.emplace_back(job.graphics, job.graphics_index);
            converter->Push(std::move(job));
            while (converter->Pending() > MaxPendingJobs && !stop.stop_requested()) {
                if (auto r = converter->Next(true)) {
                    consume(std::move(*r));
                }
            }
        };
        const auto on_member_error = [&](const std::string& error) {
            ++member_errors;
            LOG_DEBUG(Render_Vulkan, "Shader precache: {}: {}", archive.path, error);
        };
        try {
            WalkArchive(data, name, &dictionaries, on_record, on_member_error,
                        [&](std::span<const ShaderRecord> variation) {
                            if (!stop.stop_requested()) {
                                mesh_recipes.AddVariation(variation);
                            }
                        });
        } catch (const ArchiveError& e) {
            ++archive_errors;
            LOG_WARNING(Render_Vulkan, "Shader precache: {}: {}", archive.path, e.what());
        }
        drain(false);
    }
    report(StageExtracting, archives.size(), archives.size());

    const size_t total_jobs = seq;
    while (consumed < total_jobs) {
        if (stop.stop_requested()) {
            converter.reset();
            return fail("Stopped");
        }
        report(StageConverting, consumed, total_jobs);
        if (auto r = converter->Next(true)) {
            consume(std::move(*r));
        }
    }
    report(StageConverting, total_jobs, total_jobs);
    converter.reset();
    for (auto& archive : archives) {
        archive.file = nullptr;
    }
    effects.clear();
    dictionary_pack = nullptr;
    game.romfs.clear();
    game.loader.reset();
    game.nup.reset();

    // Vertex attribute types (make_graphics_warm.py attr_types): per slot, the most common
    // nonzero value among the runtime pipelines of the OTHER vertex shaders of the same
    // archive(s); slots without data are 1 (float). Without runtime data: built-in default.
    std::unordered_map<std::string, u32> archive_ids;
    for (size_t i = 0; i < archives.size(); ++i) {
        archive_ids.try_emplace(archives[i].path, static_cast<u32>(archive_ids.size()));
    }
    const auto archive_id = [&](u32 index) { return archive_ids.at(archives[index].path); };
    // Archive identity: the path inside the RomFS (the same file in both packages is one).
    std::unordered_map<u32, std::set<u64>> by_archive;
    for (const auto& program : graphics) {
        if (program.stage == 0 && program.built &&
            stats.vertex_attributes.contains(program.hash)) {
            for (const u32 a : program.archives) {
                by_archive[archive_id(a)].insert(program.hash);
            }
        }
    }
    const auto attribute_types = [&](const GraphicsProgram& program) -> u64 {
        if (!use_stats) {
            return DefaultAttributeTypes;
        }
        std::set<u64> others;
        for (const u32 a : program.archives) {
            if (const auto it = by_archive.find(archive_id(a)); it != by_archive.end()) {
                others.insert(it->second.begin(), it->second.end());
            }
        }
        others.erase(program.hash);
        std::array<std::array<size_t, 4>, 32> counts{};
        std::array<std::array<size_t, 4>, 32> first{};
        size_t order = 0;
        for (const u64 hash : others) {
            for (const u64 value : stats.vertex_attributes.at(hash)) {
                for (size_t i = 0; i < 32; ++i) {
                    const auto x = static_cast<size_t>((value >> (2 * i)) & 3);
                    if (x != 0) {
                        if (counts[i][x]++ == 0) {
                            first[i][x] = order++;
                        }
                    }
                }
            }
        }
        u64 types = 0;
        for (size_t i = 0; i < 32; ++i) {
            u64 best = 1;
            size_t best_count = 0;
            size_t best_first = 0;
            for (size_t x = 1; x < 4; ++x) {
                if (counts[i][x] > best_count ||
                    (counts[i][x] == best_count && counts[i][x] != 0 && first[i][x] < best_first)) {
                    best = x;
                    best_count = counts[i][x];
                    best_first = first[i][x];
                }
            }
            types |= best << (2 * i);
        }
        return types;
    };

    report(StageWriting, 0, written_vertices.size() + held_vertices.size());
    std::set<std::pair<u128, u64>> vertex_keys; // (environment, attribute types)
    std::vector<std::pair<u64, u64>> patches;  // key offset, attribute types
    for (const auto& written : written_vertices) {
        const auto& program = graphics[written.program];
        const u64 types = attribute_types(program);
        vertex_keys.emplace(program.environment, types);
        patches.emplace_back(written.key_offset + AttributeTypesOffset, types);
    }
    for (auto& held : held_vertices) {
        const auto& program = graphics[held.program];
        const u64 types = attribute_types(program);
        if (!vertex_keys.emplace(program.environment, types).second) {
            continue;
        }
        const auto key = GraphicsEntryKey(0, vertex_template, program.hash, types);
        Write(graphics_out, u32{1});
        graphics_out.write(reinterpret_cast<const char*>(held.env.data()),
                           static_cast<std::streamsize>(held.env.size()));
        graphics_out.write(reinterpret_cast<const char*>(key.data()),
                           static_cast<std::streamsize>(key.size()));
        held.env = {};
    }
    result.vertex_programs = vertex_keys.size();
    report("Preparing mesh candidates", 0, mesh_recipes.VariationCount());
    result.mesh_pipelines = mesh_recipes.Write(mesh_out, stop);
    result.skipped += mesh_recipes.Skipped();
    LOG_INFO(Render_Vulkan,
             "Shader mesh warm-up: {} recorded templates, {} archive variations, {} candidates",
             mesh_recipes.TemplateCount(), mesh_recipes.VariationCount(), result.mesh_pipelines);
    compute_out.close();
    graphics_out.close();
    mesh_out.close();
    if (stop.stop_requested()) {
        return fail("Stopped");
    }
    if (!compute_out || !graphics_out || !mesh_out) {
        return fail("Writing the precache files failed");
    }
    {
        std::fstream patch(graphics_tmp, std::ios::binary | std::ios::in | std::ios::out);
        if (!patch) {
            return fail("Cannot reopen " + Common::FS::PathToUTF8String(graphics_tmp));
        }
        size_t done = 0;
        for (const auto& [offset, types] : patches) {
            patch.seekp(static_cast<std::streamoff>(offset));
            patch.write(reinterpret_cast<const char*>(&types), sizeof(types));
            if (++done % 4096 == 0) {
                report(StageWriting, done, patches.size());
            }
        }
        if (!patch) {
            return fail("Writing the precache files failed");
        }
    }
    // New files: the markers and counts of the old ones no longer apply. Each is removed right
    // before its file is replaced, so a failed replacement leaves the other file's marker intact.
    std::error_code ignored;
    fs::remove(WarmMarkerPath(compute_path), ignored);
    fs::remove(WarmCountPath(compute_path), ignored);
    fs::rename(compute_tmp, compute_path, ec);
    if (ec) {
        return fail("Cannot replace " + Common::FS::PathToUTF8String(compute_path));
    }
    fs::remove(WarmMarkerPath(graphics_path), ignored);
    fs::remove(WarmCountPath(graphics_path), ignored);
    fs::rename(graphics_tmp, graphics_path, ec);
    if (ec) {
        return fail("Cannot replace " + Common::FS::PathToUTF8String(graphics_path));
    }
    WriteWarmCount(compute_path, result.compute_programs);
    WriteWarmCount(graphics_path, result.vertex_programs + result.fragment_programs);
    fs::remove(WarmMarkerPath(mesh_path), ignored);
    fs::remove(WarmCountPath(mesh_path), ignored);
    fs::rename(mesh_tmp, mesh_path, ec);
    if (ec) {
        return fail("Cannot replace " + Common::FS::PathToUTF8String(mesh_path));
    }
    WriteWarmCount(mesh_path, result.mesh_pipelines);
    report(StageWriting, patches.size(), patches.size());
    result.ok = true;
    LOG_INFO(Render_Vulkan,
             "Shader precache build for {:016x}: {} compute, {} vertex, {} fragment entries, {} "
             "programs skipped, {} archive and {} member errors, {:.1f} s",
             title_id, result.compute_programs, result.vertex_programs, result.fragment_programs,
             result.skipped, archive_errors, member_errors,
             std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count());
    return result;
}

} // Anonymous namespace

Result BuildWarmFilesWith(Core::System& system, const fs::path& game_path, u64 title_id,
                          const BuildOptions& options,
                          const std::function<void(const Progress&)>& progress,
                          std::stop_token stop) {
    if (options.full_scan) {
        return BuildFullNativeFiles(system, game_path, title_id, options, progress, stop);
    }
    // The compute precache comes first; a recipe adds the native graphics and compute pipelines.
    auto result = BuildComputeWarmFiles(system, game_path, title_id, options, progress, stop);
    const auto dir = options.output_dir.empty() ? TitleDir(title_id) : options.output_dir;
    std::error_code ec;
    auto native_options = options;
    if (native_options.recipe_path.empty() && !fs::exists(dir / "native.nx2recipe", ec) &&
        options.output_dir.empty()) {
        native_options.recipe_path = FindNativeRecipe(title_id);
    }
    if ((native_options.recipe_path.empty() && !fs::exists(dir / "native.nx2recipe", ec)) ||
        stop.stop_requested()) {
        return result;
    }
    const auto native =
        BuildNativeFiles(system, game_path, title_id, native_options, progress, stop);
    result.native_pipelines = native.native_pipelines;
    result.native_programs = native.native_programs;
    result.native_combinations = native.native_combinations;
    result.native_dropped = native.native_dropped;
    if (!native.ok) {
        result.ok = false;
        result.error = native.error;
    }
    return result;
}

} // namespace VideoCommon::ShaderWarm
