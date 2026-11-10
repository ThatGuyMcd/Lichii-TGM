// SPDX-License-Identifier: GPL-3.0-or-later
// Offline shader preparation only: no guest process, kernel initialization or game loop.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <mutex>
#include <stop_token>
#include <thread>
#include <QCoreApplication>
#include <QSaveFile>
#include <nlohmann/json.hpp>
#include "common/fs/path_util.h"
#include "common/fs/file_lock.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/scm_rev.h"
#include "shader_prepare/gpu_preparation.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/renderer_vulkan/blit_image.h"
#include "video_core/renderer_vulkan/vk_buffer_cache.h"
#include "video_core/renderer_vulkan/vk_descriptor_buffer.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_render_pass_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/shader_notify.h"
#include "video_core/oot_dlss.h"
#include "video_core/shader_warm/shader_warm_builder.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_library.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#ifdef EGLANTINE_NVIDIA_DLSS
#include "video_core/vulkan_common/nvidia_dlss.h"
#endif

#ifdef LoadString
#undef LoadString
#endif

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
fs::path Path(const std::string& value) { return fs::path{Common::FS::ToU8String(value)}; }
void Emit(Json value) {
    static std::mutex output;
    const auto line = value.dump(-1, ' ', false, Json::error_handler_t::replace);
    std::scoped_lock lock{output};
    std::cout << line << '\n' << std::flush;
}


}

int main(int argc, char** argv) try {
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 3 || application.arguments()[1] != QStringLiteral("--job")) {
        std::cerr << "Usage: lichii-tgm-shader-prepare --job <job.json>\n";
        return 2;
    }
    const fs::path job_path{application.arguments()[2].toStdU16String()};
    if (fs::file_size(job_path) > 2 * 1024 * 1024) throw std::runtime_error("Preparation job is too large.");
    std::ifstream input{job_path};
    const Json job = Json::parse(input);
    if (job.at("format") != 1) throw std::runtime_error("Unknown preparation job format.");
    const auto expected_source = job.value("expected_source", std::string{});
    if (!expected_source.empty() && expected_source != Common::g_scm_rev)
        throw std::runtime_error("The shader worker is from a different build. Extract the complete emulator package.");
    const u64 title = std::stoull(job.at("title_id").get<std::string>(), nullptr, 16);
    const fs::path job_directory = Path(job.at("job_directory").get<std::string>());
    fs::create_directories(job_directory / "log");
    for (const auto& [name, value] : job.at("paths").items()) {
        const int key = std::stoi(name);
        if (key < 0 || key > static_cast<int>(Common::FS::EdenPath::FidelityFxDir))
            throw std::runtime_error("Invalid preparation data path.");
        Common::FS::SetEdenPath(static_cast<Common::FS::EdenPath>(key), Path(value.get<std::string>()));
    }
    Common::FS::SetEdenPath(Common::FS::EdenPath::LogDir, job_directory / "log");
    Common::Log::Initialize();
    Common::Log::SetColorConsoleBackendEnabled(false);
    Common::Log::Start();
    for (const auto& [key, value] : job.at("settings").items()) {
        if (const auto found = Settings::values.linkage.by_key.find(key);
            found != Settings::values.linkage.by_key.end()) {
            found->second->SetGlobal(true);
            found->second->LoadString(value.get<std::string>());
        }
    }
    Settings::values.disabled_addons[title] = job.value("disabled_addons", std::vector<std::string>{});
    Settings::values.external_content_dirs = job.value("external_content_dirs", std::vector<std::string>{});
    Settings::SetCurrentProgramID(title);
    Settings::SetCurrentProgramIsSwitch2(true);
    if (title == VideoCommon::OotDlss::ProgramId && VideoCommon::OotDlss::Requested() &&
        std::getenv("EDEN_RES_SETUP") == nullptr) {
        Settings::values.resolution_setup.SetGlobal(true);
        Settings::values.resolution_setup.SetValue(Settings::ResolutionSetup::Res3_2X);
    }
    Settings::UpdateRescalingInfo();
    Settings::UpdateGPUAccuracy();
    const auto title_directory = Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir) /
        QString::number(title, 16).rightJustified(16, QLatin1Char('0')).toStdString();
    fs::create_directories(title_directory);
    Common::FS::FileLock preparation_lock(title_directory / "native-preparation.lock");
    if (!preparation_lock.Held()) throw std::runtime_error("Another instance is using this game's shader cache. Close that game or preparation job first.");
    Common::FS::FileLock runtime_cache_lock(title_directory / "lichii-cache.v2.bin.lock");
    if (!runtime_cache_lock.Held()) throw std::runtime_error("This game's pipeline cache is active in another emulator instance. Close it before preparation.");
    std::stop_source cancellation;
    std::jthread watcher{[&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::error_code error;
            if (fs::exists(job_directory / "cancel", error)) cancellation.request_stop();
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
    }};
    Core::System system;
    system.SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    system.GetFileSystemController().CreateFactories(*system.GetFilesystem());
    namespace Warm = VideoCommon::ShaderWarm;
    Warm::BuildOptions options;
    options.full_scan = true;
    options.threads = std::clamp<size_t>(job.value("workers", 4), 1, 8);
    if (title == 0x0400486024DEC000ULL) {
        Warm::NativeRecipeProfile profile;
        profile.title_id = title;
        profile.game_version = 131072;
        profile.cache_version = 54;
        profile.main_build_id = "E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348";
        profile.expected_recipe_sha256 = "85d944c8557fbeffe02af566c9c4277251ed0ab1a0f2a3ab7a626af6a877c3a1";
        profile.recipe_path = fs::path{QCoreApplication::applicationDirPath().toStdU16String()} /
                              "recipes/oot/oot-v131072-cache54.nx2recipe";
        options.native_profile = std::move(profile);
    }
    auto last = std::chrono::steady_clock::time_point{};
    const auto scanned = Warm::BuildWarmFilesWith(system, Path(job.at("game_path").get<std::string>()),
        title, options, [&](const Warm::Progress& progress) {
            const auto now = std::chrono::steady_clock::now();
            if (now - last < std::chrono::milliseconds{100} && progress.done != progress.total) return;
            last = now;
            Emit({{"event", "progress"}, {"stage", progress.stage}, {"done", progress.done}, {"total", progress.total}});
        }, cancellation.get_token());
    Json result{{"event", "result"}, {"ok", scanned.ok}, {"partial", scanned.partial},
        {"release", std::string{Common::g_build_fullname}},
        {"source_commit", std::string{Common::g_scm_rev}},
        {"scan_finished", scanned.scan_finished}, {"native_ready", scanned.native_ready},
        {"files_scanned", scanned.files_scanned},
        {"programs", std::accumulate(scanned.stage_programs.begin(), scanned.stage_programs.end(), size_t{})},
        {"stage_programs", scanned.stage_programs}, {"combinations", scanned.native_combinations},
        {"scan_errors", scanned.scan_errors}, {"limited_files", scanned.limited_files},
        {"recipe_programs_missing", scanned.recipe_programs_missing},
        {"recipe_programs_failed", scanned.recipe_programs_failed},
        {"recipe_records_dropped", scanned.native_dropped},
        {"total", scanned.native_pipelines}, {"built", 0}, {"already_prepared", 0},
        {"failed", 0}, {"incompatible", 0}, {"quarantined", 0}, {"unreached", 0},
        {"warnings", scanned.warnings}, {"error", scanned.error}};
    const bool vulkan_enabled = Settings::values.renderer_backend.GetValue() == Settings::RendererBackend::Vulkan;
    const bool persistent_enabled = Settings::values.use_disk_shader_cache.GetValue() &&
                                    Settings::values.use_vulkan_driver_pipeline_cache.GetValue();
    const bool prepare_gpu = job.value("prepare_gpu", true);
    if (scanned.native_ready && (!vulkan_enabled || !persistent_enabled || !prepare_gpu)) {
        result["partial"] = true;
        result["unreached"] = scanned.native_pipelines;
        result["warnings"].push_back(prepare_gpu
            ? "GPU preparation requires Vulkan and persistent shader/driver caches. Enable those settings before preparing this game's pipelines."
            : "GPU preparation was deferred for this scan. The assembled records are ready for a later preparation job.");
    }
    if (scanned.ok && scanned.native_ready && prepare_gpu && vulkan_enabled && persistent_enabled &&
        !cancellation.stop_requested()) {
        try {
            std::string device_name;
            const auto gpu = ShaderPreparation::PrepareGpuPipelines(title_directory, options.threads,
                cancellation.get_token(),
                [](VideoCore::LoadCallbackStage, size_t done, size_t total) {
                    Emit({{"event", "progress"}, {"stage", "Preparing pipelines on the GPU"},
                          {"done", done}, {"total", total}});
                }, [&device_name](std::string_view name) {
                    device_name = name;
                    Emit({{"event", "device"}, {"name", name}});
                });
            result["gpu"] = device_name;
            result["total"] = gpu.total;
            result["built"] = gpu.built;
            result["already_prepared"] = gpu.already_prepared;
            result["failed"] = gpu.failed;
            result["incompatible"] = gpu.incompatible;
            result["quarantined"] = gpu.quarantined;
            result["unreached"] = gpu.unreached;
            if (!gpu.error.empty()) {
                result["ok"] = false;
                result["error"] = gpu.error;
            }
            if (gpu.total == 0 && scanned.native_pipelines != 0 && !cancellation.stop_requested()) {
                result["ok"] = false;
                result["error"] = "The GPU worker did not read any of the assembled pipeline records.";
            }
            result["partial"] = scanned.partial || gpu.failed || gpu.incompatible || gpu.quarantined || gpu.unreached;
        } catch (const std::exception& error) {
            result["ok"] = false;
            result["error"] = error.what();
        }
    }
    result["cancelled"] = cancellation.stop_requested();
    const auto report = job_directory / "report.json";
    QSaveFile output(QString::fromStdU16String(report.u16string()));
    const auto data = result.dump(2, ' ', false, Json::error_handler_t::replace);
    if (!output.open(QIODevice::WriteOnly) ||
        output.write(data.data(), static_cast<qint64>(data.size())) != static_cast<qint64>(data.size()) ||
        !output.commit()) {
        result["warnings"].push_back("The detailed report could not be saved; the cache checkpoints are retained.");
    }
    Emit(result);
    return result["ok"].get<bool>() && !cancellation.stop_requested() ? 0 : 1;
} catch (const std::exception& error) {
    Emit({{"event", "result"}, {"ok", false}, {"error", error.what()}});
    return 1;
}
