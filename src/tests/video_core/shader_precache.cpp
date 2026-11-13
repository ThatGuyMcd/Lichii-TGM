// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <openssl/evp.h>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/exception.h"
#include "common/zstd_compression.h"
#include "common/fs/file_lock.h"
#include "core/core.h"
#include "core/file_sys/nx2_shader_extract.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/shader_environment.h"
#include "video_core/shader_warm/shader_archives.h"
#include "video_core/shader_warm/shader_warm_builder.h"
#include "video_core/shader_warm/warm_records.h"

namespace {

namespace ShaderWarm = VideoCommon::ShaderWarm;
using Settings::PrecacheShaders;

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("eden-shader-precache-" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void WriteBytes(const std::filesystem::path& file, std::string_view bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // Anonymous namespace

TEST_CASE("ShaderPrecache: the setting decides whether the boot precaches", "[video_core]") {
    STATIC_REQUIRE_FALSE(ShaderWarm::PrecacheBeforeStart(PrecacheShaders::Off, false));
    STATIC_REQUIRE_FALSE(ShaderWarm::PrecacheBeforeStart(PrecacheShaders::Off, true));
    STATIC_REQUIRE_FALSE(ShaderWarm::PrecacheBeforeStart(PrecacheShaders::Ask, false));
    STATIC_REQUIRE(ShaderWarm::PrecacheBeforeStart(PrecacheShaders::Ask, true));
    STATIC_REQUIRE(ShaderWarm::PrecacheBeforeStart(PrecacheShaders::Always, false));
    STATIC_REQUIRE(ShaderWarm::PrecacheBeforeStart(PrecacheShaders::Always, true));
}

TEST_CASE("ShaderPrecache: launch requests are taken once", "[video_core]") {
    ShaderWarm::RequestWarmBeforeStart(true);
    REQUIRE(ShaderWarm::TakeWarmBeforeStart());
    REQUIRE_FALSE(ShaderWarm::TakeWarmBeforeStart());

    ShaderWarm::RequestSkipWarm();
    REQUIRE(ShaderWarm::TakeSkipWarm());
    REQUIRE_FALSE(ShaderWarm::TakeSkipWarm());

    // A skip pressed during an earlier launch does not skip the next one.
    ShaderWarm::RequestSkipWarm();
    ShaderWarm::RequestWarmBeforeStart(true);
    REQUIRE_FALSE(ShaderWarm::TakeSkipWarm());
    REQUIRE(ShaderWarm::TakeWarmBeforeStart());
}

TEST_CASE("ShaderPrecache: the marker matches only the same file and driver", "[video_core]") {
    TempDir dir;
    const auto warm = dir.path / "warm-compute.bin";
    REQUIRE_FALSE(ShaderWarm::CurrentWarmStamp(warm, 0, 1, 2, 3).has_value());
    REQUIRE_FALSE(ShaderWarm::ReadWarmMarker(warm).has_value());

    WriteBytes(warm, "precache");
    const auto stamp = ShaderWarm::CurrentWarmStamp(warm, 12, 0x1234, 0x2206, 53);
    REQUIRE(stamp.has_value());
    REQUIRE(stamp->size == 8);
    REQUIRE(ShaderWarm::WriteWarmMarker(warm, *stamp));
    REQUIRE(ShaderWarm::WarmMarkerPath(warm) == dir.path / "warm-compute.done");

    const auto marker = ShaderWarm::ReadWarmMarker(warm);
    REQUIRE(marker.has_value());
    REQUIRE(marker->entries == 12);
    // The entry count is informative: a stamp taken before reading the file still matches.
    REQUIRE(marker->Matches(*ShaderWarm::CurrentWarmStamp(warm, 0, 0x1234, 0x2206, 53)));
    REQUIRE_FALSE(marker->Matches(*ShaderWarm::CurrentWarmStamp(warm, 12, 0x1235, 0x2206, 53)));
    REQUIRE_FALSE(marker->Matches(*ShaderWarm::CurrentWarmStamp(warm, 12, 0x1234, 0x2207, 53)));
    REQUIRE_FALSE(marker->Matches(*ShaderWarm::CurrentWarmStamp(warm, 12, 0x1234, 0x2206, 54)));

    WriteBytes(warm, "a rebuilt precache");
    REQUIRE_FALSE(marker->Matches(*ShaderWarm::CurrentWarmStamp(warm, 12, 0x1234, 0x2206, 53)));
}

TEST_CASE("ShaderPrecache: precached pipelines are kept per driver", "[video_core]") {
    TempDir dir;
    const auto file = dir.path / "precache-graphics.bin";
    const ShaderWarm::DriverStamp stamp{0x1234, 0x2206, 53};
    REQUIRE(ShaderWarm::ReadPrecachedPipelines(file, stamp).empty());

    const std::unordered_set<u64> hashes{1, 0xfedcba9876543210ULL, 42};
    REQUIRE(ShaderWarm::WritePrecachedPipelines(file, stamp, hashes));
    REQUIRE(ShaderWarm::ReadPrecachedPipelines(file, stamp) == hashes);
    REQUIRE(ShaderWarm::ReadPrecachedPipelines(file, {0x1235, 0x2206, 53}).empty());
    REQUIRE(ShaderWarm::ReadPrecachedPipelines(file, {0x1234, 0x2207, 53}).empty());
    REQUIRE(ShaderWarm::ReadPrecachedPipelines(file, {0x1234, 0x2206, 54}).empty());

    // A truncated file keeps the hashes it holds in full.
    std::filesystem::resize_file(file, std::filesystem::file_size(file) - 4);
    REQUIRE(ShaderWarm::ReadPrecachedPipelines(file, stamp).size() == 2);
}

TEST_CASE("ShaderPrecache: content and emission variants invalidate warm markers", "[video_core]") {
    TempDir dir;
    const auto warm = dir.path / "warm-compute.bin";
    const auto replacement = dir.path / "warm-compute.tmp";
    WriteBytes(warm, "precache");
    const auto original = ShaderWarm::CurrentWarmStamp(warm, 12, 1, 2, 3, 1, 2, 77);
    REQUIRE(original);
    REQUIRE(ShaderWarm::WriteWarmMarker(warm, *original));
    WriteBytes(replacement, "precache");
    ShaderWarm::DropStaleWarmMarker(warm, replacement);
    const auto marker = ShaderWarm::ReadWarmMarker(warm);
    REQUIRE(marker);
    REQUIRE(marker->Matches(*original));
    REQUIRE_FALSE(marker->Matches(*ShaderWarm::CurrentWarmStamp(warm, 12, 1, 2, 3, 1, 2, 78)));
    REQUIRE_FALSE(marker->Matches(*ShaderWarm::CurrentWarmStamp(warm, 12, 1, 2, 3, 0, 2, 77)));
    REQUIRE_FALSE(marker->Matches(*ShaderWarm::CurrentWarmStamp(warm, 12, 1, 2, 3, 1, 0, 77)));
    const auto time = std::filesystem::last_write_time(warm);
    WriteBytes(warm, "precachf");
    std::filesystem::last_write_time(warm, time);
    REQUIRE_FALSE(marker->Matches(*ShaderWarm::CurrentWarmStamp(warm, 12, 1, 2, 3, 1, 2, 77)));
    WriteBytes(replacement, "precachf");
    ShaderWarm::DropStaleWarmMarker(warm, replacement);
    REQUIRE_FALSE(ShaderWarm::ReadWarmMarker(warm));
    WriteBytes(ShaderWarm::WarmMarkerPath(warm), "8 133981234567890000 12 1 2 3 1 2 persist1\n");
    REQUIRE_FALSE(ShaderWarm::ReadWarmMarker(warm));
    REQUIRE(ShaderWarm::EmissionStamp() != 0);
}

TEST_CASE("ShaderPrecache: the entry count belongs to a file of one size", "[video_core]") {
    TempDir dir;
    const auto warm = dir.path / "warm-compute.bin";
    WriteBytes(warm, "0123456789");
    REQUIRE(ShaderWarm::ReadWarmCount(warm) == 0);

    WriteBytes(ShaderWarm::WarmCountPath(warm), "345 10\n");
    REQUIRE(ShaderWarm::ReadWarmCount(warm) == 345);

    WriteBytes(warm, "01234567890");
    REQUIRE(ShaderWarm::ReadWarmCount(warm) == 0);
}

TEST_CASE("ShaderPrecache: a compute entry reads back like a pipeline cache entry",
          "[video_core]") {
    ShaderWarm::LoweredCode lowered;
    lowered.code.resize(16 * sizeof(u64));
    for (size_t i = 0; i < lowered.code.size(); ++i) {
        lowered.code[i] = static_cast<u8>(i * 7 + 1);
    }
    lowered.texture_buffer = 3;
    const ShaderWarm::ComputeControl control{{64, 2, 1}, 1024, 256};
    ShaderWarm::ComputeKey key_bytes{};
    const auto entry = ShaderWarm::ComputeEntry(lowered, control, {}, key_bytes);

    TempDir dir;
    const auto file = dir.path / "entry.bin";
    WriteBytes(file, std::string_view{reinterpret_cast<const char*>(entry.data()), entry.size()});

    std::ifstream in(file, std::ios::binary);
    in.exceptions(std::ifstream::failbit);
    u32 num_envs{};
    in.read(reinterpret_cast<char*>(&num_envs), sizeof(num_envs));
    REQUIRE(num_envs == 1);
    VideoCommon::FileEnvironment env;
    env.Deserialize(in, static_cast<std::streamoff>(entry.size()));
    Vulkan::ComputePipelineCacheKey key{};
    static_assert(sizeof(key) == sizeof(key_bytes));
    in.read(reinterpret_cast<char*>(&key), sizeof(key));
    REQUIRE(in.tellg() == static_cast<std::streamoff>(entry.size()));

    REQUIRE(env.ShaderStage() == Shader::Stage::Compute);
    REQUIRE(env.WorkgroupSize() == control.workgroup);
    REQUIRE(env.SharedMemorySize() == control.shared_memory);
    REQUIRE(env.LocalMemorySize() == control.local_memory);
    REQUIRE(env.TextureBoundBuffer() == 3);
    REQUIRE(std::memcmp(&key, key_bytes.data(), sizeof(key)) == 0);
    REQUIRE(key.unique_hash == ShaderWarm::StageHash(lowered.code));
    REQUIRE(key.shared_memory_size == control.shared_memory);
    REQUIRE(key.workgroup_size == control.workgroup);

    // The entry has no recorded reads: without guesses they are errors, with them the guesses.
    REQUIRE_THROWS_AS(env.ReadCbufValue(0, 0x40), Shader::LogicError);
    static const VideoCommon::GuessMissing guess{};
    env.SetGuessMissing(&guess);
    REQUIRE(env.ReadCbufValue(0, 0x40) == guess.cbuf_bank0);
    REQUIRE(env.ReadCbufValue(3, 0x40) == guess.cbuf_other);
    REQUIRE(env.ReadTextureType(5) == guess.texture_type);
    REQUIRE(env.IsCbufStorageBufferPointer(0, 0x10) == guess.storage_pointer_bank0);
    REQUIRE(env.IsCbufStorageBufferPointer(1, 0x10) == guess.storage_pointer_other);
}

TEST_CASE("ShaderPrecache: a program naming no texture bank reads bank 0", "[video_core]") {
    ShaderWarm::LoweredCode lowered;
    lowered.code.resize(16 * sizeof(u64));
    ShaderWarm::ComputeKey key{};
    const auto entry = ShaderWarm::ComputeEntry(lowered, {{1, 1, 1}, 0, 0}, {}, key);
    u32 texture_bound{};
    // num_envs, then six u64 counts and local memory before the texture bank.
    std::memcpy(&texture_bound, entry.data() + 4 + 6 * 8 + 4, sizeof(texture_bound));
    REQUIRE(texture_bound == 0);
}

TEST_CASE("ShaderPrecache: every container the archive walker reads is discovered",
          "[video_core]") {
    using ShaderWarm::ClassifyGameFile;
    using ShaderWarm::GameFileKind;
    for (const char* path : {"Shader/Model.product.nin_ounce_nvn2.bfsha.zs",
                             "Shader/Model.bfsha", "Pack/Shaders.nin_ounce_nvn2.release.sarc.zs",
                             "Pack/Shaders.release.sarc", "Shader/Loose.bnsh",
                             "Shader/Loose.bnsh.zs", "Shader/Bundle.cgsb", "Shader/Bundle.cgsb.zs",
                             "Shader/Program.shn", "Shader/Program.shn.zs", "Shader/Archive.sharcb",
                             "Shader/Archive.sharcb.zs", "Shader/UPPER.BNSH", "Effect/Fx.bfsha.zs",
                             "Shader/Model.bfsha.zst", "Shader/UPPER.BFSHA.ZST"}) {
        INFO(path);
        REQUIRE(ClassifyGameFile(path) == GameFileKind::ShaderArchive);
    }
    REQUIRE(ClassifyGameFile("Effect/Fx.nin_ounce_nvn2.vsetb.byml.zs") == GameFileKind::Effect);
    REQUIRE(ClassifyGameFile("Pack/ZsDic.pack.zs") == GameFileKind::ZstdDictionaries);
    for (const char* path :
         {"Effect/Sub/Fx.vsetb.byml.zs", "Model/Fx.vsetb.byml.zs", "Effect/Fx.vsetb.byml",
          "Model/Tex.bntx.zs", "Model/Mesh.bfres.zs", "Asset/assets_a000.dat",
          "Bin/Nvn2-upper_left.ndsr", "Shader/notes.shn.txt", "Shader/bnsh"}) {
        INFO(path);
        REQUIRE(ClassifyGameFile(path) == GameFileKind::Other);
    }
}

TEST_CASE("ShaderPrecache: programs that cannot be lowered are refused", "[video_core]") {
    ShaderWarm::LoweredCode lowered;
    REQUIRE(ShaderWarm::LowerProgram({}, lowered).has_value());
    const std::array<u8, 15> partial{};
    REQUIRE(ShaderWarm::LowerProgram(partial, lowered).has_value());
    // One instruction with no exit: not a complete program.
    const std::array<u8, 16> no_exit{};
    REQUIRE(ShaderWarm::LowerProgram(no_exit, lowered).has_value());
    REQUIRE(lowered.code.empty());
}

namespace {

std::vector<u8> BareCompute(u32 instruction) {
    std::vector<u8> bytes(0x64);
    const u32 magic = 0x98761234, version = 0xc9, control_size = 0x60, code_size = 4;
    std::memcpy(bytes.data(), &magic, 4);
    std::memcpy(bytes.data() + 4, &version, 4);
    std::memcpy(bytes.data() + 0x30, &control_size, 4);
    std::memcpy(bytes.data() + 0x44, &code_size, 4);
    bytes[0x56] = 5;
    std::memcpy(bytes.data() + control_size, &instruction, 4);
    return bytes;
}

std::vector<u8> RecipeHeader(u64 title, u32 game_version, u32 cache_version) {
    std::vector<u8> body(20);
    const u32 format = 1;
    std::memcpy(body.data(), &format, 4);
    std::memcpy(body.data() + 4, &title, 8);
    std::memcpy(body.data() + 12, &game_version, 4);
    std::memcpy(body.data() + 16, &cache_version, 4);
    const auto compressed =
        Common::Compression::CompressDataZSTDDefault(body.data(), body.size());
    std::vector<u8> recipe(16);
    std::memcpy(recipe.data(), "NX2RCP01", 8);
    const u32 size = static_cast<u32>(body.size());
    std::memcpy(recipe.data() + 8, &size, 4);
    recipe.insert(recipe.end(), compressed.begin(), compressed.end());
    return recipe;
}

std::string RecipeHash(std::span<const u8> recipe) {
    std::array<u8, 32> digest{};
    unsigned size{};
    REQUIRE(EVP_Digest(recipe.data(), recipe.size(), digest.data(), &size, EVP_sha256(), nullptr));
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const u8 byte : digest) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}

} // Anonymous namespace

TEST_CASE("Full native scanning finds extensionless programs and reports memory limits",
          "[video_core][shader_scan]") {
    namespace X = FileSys::Nx2ShaderExtract;
    const auto bytes = BareCompute(0x12345678);
    auto root = std::make_shared<FileSys::VectorVfsDirectory>();
    root->AddFile(std::make_shared<FileSys::VectorVfsFile>(bytes, "not-a-shader-extension"));
    root->AddFile(std::make_shared<FileSys::VectorVfsFile>(bytes, "another.copy"));
    const std::array<FileSys::VirtualDir, 1> roots{root};
    const auto found = X::Extract(roots, 1, nullptr, {}, true);
    REQUIRE(found.programs.size() == 1);
    REQUIRE(found.programs.front().stage == X::Stage::Compute);
    REQUIRE(found.stats.files == 2);
    REQUIRE(found.stats.occurrences == 2);
    REQUIRE(found.stats.retained_bytes >= bytes.size());
    REQUIRE(found.errors.empty());
    const auto limited = X::Extract(roots, 1, nullptr, {}, true,
                                    {.max_blob_bytes = 1024, .max_retained_bytes = 1});
    REQUIRE(limited.programs.empty());
    REQUIRE(limited.stats.programs_limited == 2);
    REQUIRE_FALSE(limited.errors.empty());
    REQUIRE(root->GetFile("another.copy")->ReadAllBytes() == bytes);
}

TEST_CASE("Full native scanning reports oversized containers and respects cancellation",
          "[video_core][shader_scan]") {
    namespace X = FileSys::Nx2ShaderExtract;
    std::vector<u8> bytes(128);
    std::memcpy(bytes.data(), "SARC", 4);
    auto root = std::make_shared<FileSys::VectorVfsDirectory>();
    root->AddFile(std::make_shared<FileSys::VectorVfsFile>(bytes, "oversized.sarc"));
    const std::array<FileSys::VirtualDir, 1> roots{root};
    const auto limited = X::Extract(roots, 1, nullptr, {}, true,
                                    {.max_blob_bytes = 64, .max_retained_bytes = 1024});
    REQUIRE(limited.stats.limited_files == 1);
    REQUIRE_FALSE(limited.errors.empty());
    std::stop_source cancel;
    cancel.request_stop();
    const auto stopped = X::Extract(roots, 1, nullptr, cancel.get_token(), true);
    REQUIRE(stopped.stats.files == 0);
    REQUIRE(stopped.programs.empty());
}

TEST_CASE("Full scanning inspects compressed data without a known container prefix",
          "[video_core][shader_scan]") {
    namespace X = FileSys::Nx2ShaderExtract;
    auto bytes = BareCompute(0x12345678);
    const auto compressed =
        Common::Compression::CompressDataZSTDDefault(bytes.data(), bytes.size());
    auto root = std::make_shared<FileSys::VectorVfsDirectory>();
    root->AddFile(std::make_shared<FileSys::VectorVfsFile>(compressed, "data.bin"));
    const std::array<FileSys::VirtualDir, 1> roots{root};
    REQUIRE(X::Extract(roots, 1, nullptr, {}, true).programs.empty());
    const auto full = X::Extract(roots, 1, nullptr, {}, true,
                                 {.max_blob_bytes = 1024, .max_retained_bytes = 4096,
                                  .inspect_all_compressed = true});
    REQUIRE(full.programs.size() == 1);
    REQUIRE(full.errors.empty());
    REQUIRE(full.stats.peeked_only == 0);
    const auto limited = X::Extract(roots, 1, nullptr, {}, true,
                                    {.max_blob_bytes = bytes.size() - 1, .max_retained_bytes = 4096,
                                     .inspect_all_compressed = true});
    REQUIRE(limited.programs.empty());
    REQUIRE_FALSE(limited.errors.empty());
}

TEST_CASE("Full preparation refuses recipes with mismatched provenance or game identity",
          "[video_core][shader_scan]") {
    constexpr u64 title = 0x0400486024DEC000;
    const auto recipe = RecipeHeader(title, 131072, ShaderWarm::WarmCacheVersion);
    ShaderWarm::NativeRecipeProfile profile{
        .title_id = title, .game_version = 131072,
        .cache_version = ShaderWarm::WarmCacheVersion, .main_build_id = "verified-build",
        .recipe_path = {}, .expected_recipe_sha256 = RecipeHash(recipe)};
    REQUIRE_FALSE(ShaderWarm::ValidateNativeRecipeProfile(
        profile, recipe, title, 131072, "verified-build"));
    REQUIRE(ShaderWarm::ValidateNativeRecipeProfile(profile, recipe, title, 0, "verified-build"));
    REQUIRE(ShaderWarm::ValidateNativeRecipeProfile(profile, recipe, title + 1, 131072,
                                                    "verified-build"));
    REQUIRE(ShaderWarm::ValidateNativeRecipeProfile(profile, recipe, title, 131072, "other-build"));
    auto altered = recipe;
    altered[0] ^= 1;
    REQUIRE(ShaderWarm::ValidateNativeRecipeProfile(profile, altered, title, 131072,
                                                    "verified-build"));
    profile.expected_recipe_sha256 = "wrong hash";
    REQUIRE(ShaderWarm::ValidateNativeRecipeProfile(profile, recipe, title, 131072,
                                                    "verified-build"));
    profile.expected_recipe_sha256 = RecipeHash(recipe);
    ++profile.cache_version;
    REQUIRE(ShaderWarm::ValidateNativeRecipeProfile(profile, recipe, title, 131072,
                                                    "verified-build"));
}

TEST_CASE("Quick preparation cannot overwrite a cache held by full preparation",
          "[video_core][shader_scan]") {
    TempDir temporary;
    Common::FS::FileLock lock(temporary.path / "native-preparation.lock");
    REQUIRE(lock.Held());
    auto system = std::make_unique<Core::System>();
    ShaderWarm::BuildOptions options;
    options.output_dir = temporary.path;
    const auto result = ShaderWarm::BuildWarmFilesWith(
        *system, temporary.path / "missing-game.nup", 0x0400486024DEC000, options, {}, {});
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("locked") != std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(temporary.path / "native-preparation.bin"));
}
