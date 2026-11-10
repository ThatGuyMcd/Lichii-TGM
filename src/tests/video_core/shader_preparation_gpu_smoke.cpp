// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "shader_prepare/gpu_preparation.h"
#include "shader_recompiler/frontend/ampere/instruction.h"
#include "video_core/pipeline_cache_files.h"
#include "video_core/shader_warm/warm_records.h"

namespace {
namespace fs = std::filesystem;
namespace Warm = VideoCommon::ShaderWarm;

void Require(bool condition, std::string_view reason) {
    if (!condition) {
        throw std::runtime_error(std::string{reason});
    }
}

void RequireInside(const fs::path& root, const fs::path& path) {
    const auto target = fs::weakly_canonical(path);
    auto part = target.begin();
    for (const auto& component : root) {
        Require(part != target.end() && *part == component,
                "A smoke-test path escapes the explicitly selected scratch folder");
        ++part;
    }
    Require(part != target.end(), "The scratch folder itself is not a removable test file");
}

template <typename T>
void Append(std::vector<u8>& bytes, T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::endian::native == std::endian::little);
    const auto offset = bytes.size();
    bytes.resize(offset + sizeof(value));
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void WriteFixture(const fs::path& scratch, const fs::path& directory) {
    // The synthetic PlainProgram already used by shader_cache_v2.cpp: a convergent branch and
    // two exits, with no memory operations, bindings, game code or GPU dispatch.
    using Shader::Ampere::Instruction;
    constexpr Instruction exit{{0x0000794d, 0, 0x03800000, 0}};
    constexpr std::array<Instruction, 3> program{{
        {{0x06007947, 0x13, 0x0b800000, 0}}, exit, exit,
    }};
    const std::span<const u8> native{reinterpret_cast<const u8*>(program.data()), sizeof(program)};
    Warm::LoweredCode lowered;
    if (const auto error = Warm::LowerProgram(native, lowered)) {
        throw std::runtime_error("Synthetic program did not lower: " + *error);
    }
    Warm::ComputeKey key{};
    const auto entry = Warm::ComputeEntry(lowered, {{1, 1, 1}, 0, 0}, {}, key);
    Require(entry.size() > 4 + key.size(), "Synthetic environment is empty");
    const auto environment = std::span{entry}.subspan(4, entry.size() - 4 - key.size());
    constexpr u64 environment_offset = 24 + 16 + 4 + 4 + 24;
    std::vector<u8> bytes{'N', 'X', '2', 'P', 'R', 'E', 'P', '1'};
    Append<u32>(bytes, VideoCommon::VulkanPipelineCacheVersion);
    Append<u32>(bytes, 1); // environment count
    Append<u32>(bytes, 1); // record count
    Append<u32>(bytes, 0);
    Append<u64>(bytes, environment_offset);
    Append<u64>(bytes, environment.size());
    Append<u8>(bytes, 1); // compute record
    Append<u8>(bytes, 1);
    Append<u16>(bytes, 0);
    Append<u32>(bytes, 0); // environment index
    bytes.insert(bytes.end(), key.begin(), key.end());
    Require(bytes.size() == environment_offset, "Synthetic preparation offsets disagree");
    bytes.insert(bytes.end(), environment.begin(), environment.end());
    const auto path = directory / "native-preparation.bin";
    RequireInside(scratch, path);
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    Require(static_cast<bool>(output), "Cannot write the synthetic preparation file");
}

Vulkan::NativePreparationResult Prepare(const fs::path& directory) {
    return ShaderPreparation::PrepareGpuPipelines(directory, 1, {},
        [](VideoCore::LoadCallbackStage, size_t, size_t) {},
        [](std::string_view name) { std::cout << "GPU: " << name << '\n'; });
}

void Check(const Vulkan::NativePreparationResult& result, size_t built, size_t previous,
           std::string_view phase) {
    if (!result.error.empty()) {
        throw std::runtime_error(std::string{phase} + ": " + result.error);
    }
    Require(!result.cancelled, "Synthetic preparation was unexpectedly cancelled");
    Require(result.total == 1 && result.built == built && result.already_prepared == previous &&
            result.failed == 0 && result.incompatible == 0 && result.quarantined == 0 &&
            result.unreached == 0, "Synthetic preparation counters disagree");
    std::cout << phase << ": total=" << result.total << " built=" << result.built
              << " already_prepared=" << result.already_prepared << '\n';
}

int Run(const fs::path& requested) {
    Require(requested.is_absolute(), "Pass an absolute, explicitly named scratch folder");
    const auto scratch = fs::weakly_canonical(requested);
    Require(scratch != scratch.root_path() && !scratch.filename().empty(),
            "A filesystem root cannot be used as the scratch folder");
    Require(!fs::exists(scratch) || (fs::is_directory(scratch) && fs::is_empty(scratch)),
            "Use a new or empty scratch folder; existing contents will not be changed");
    fs::create_directories(scratch);
    const auto directory = scratch / "pipelines";
    RequireInside(scratch, directory);
    fs::create_directory(directory);
    for (const auto& [kind, name] : std::array{
             std::pair{Common::FS::EdenPath::LogDir, "log"},
             std::pair{Common::FS::EdenPath::ShaderDir, "shader"},
             std::pair{Common::FS::EdenPath::DumpDir, "dump"}}) {
        const auto path = scratch / name;
        RequireInside(scratch, path);
        fs::create_directory(path);
        Common::FS::SetEdenPath(kind, path);
    }
    Common::Log::Initialize();
    Common::Log::SetColorConsoleBackendEnabled(false);
    Common::Log::Start();
    Settings::values.use_disk_shader_cache.SetValue(true);
    Settings::values.use_vulkan_driver_pipeline_cache.SetValue(true);
    Settings::values.dump_guest_shaders.SetValue(false);
    Settings::UpdateRescalingInfo();
    Settings::UpdateGPUAccuracy();
    WriteFixture(scratch, directory);
    Check(Prepare(directory), 1, 0, "cold preparation");
    Check(Prepare(directory), 0, 1, "warm preparation");
    const auto driver_cache = VideoCommon::VersionedPipelineCachePath(
        directory, "vulkan_pipelines", VideoCommon::VulkanPipelineCacheVersion);
    RequireInside(scratch, driver_cache);
    Require(fs::is_regular_file(driver_cache), "The driver cache was not saved");
    // Delete only this newly created fixture's driver file. Its CPU journal remains, proving
    // that the next preparation cannot mistake that journal for a surviving driver cache.
    Require(fs::remove(driver_cache), "Cannot remove the smoke-test driver cache");
    Check(Prepare(directory), 1, 0, "preparation after driver-cache deletion");
    std::cout << "GPU preparation smoke test passed; artifacts remain in " << scratch << '\n';
    return 0;
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    try {
        Require(argc == 2, "Usage: shader-preparation-gpu-smoke <absolute empty scratch folder>");
        return Run(fs::path{argv[1]});
    } catch (const std::exception& error) {
        std::cerr << "GPU preparation smoke test failed: " << error.what() << '\n';
        return 1;
    }
}
