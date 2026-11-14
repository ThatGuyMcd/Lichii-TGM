// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "common/common_funcs.h"
#include "common/fs/path_util.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/deconstructed_rom_directory.h"
#include "core/loader/nso.h"
#include "core/oot_fps_patches.h"
#include "core/oot_ultrawide.h"
#include "tests/core/system_test_context.h"

namespace {
constexpr u64 TitleId = 0x0400486024DEC000;
constexpr u32 MainDataLocation = 0x4D46000;
constexpr u32 MainDataSize = 0xB80;
constexpr u32 MainBssSize = 1414272;
constexpr u32 SmallModuleSize = 0x4000;
constexpr std::array<u8, 32> MainBuildId{
    0xE0, 0xF3, 0xCC, 0xFA, 0xF5, 0x00, 0x65, 0xAE, 0x4E, 0xA0,
    0x1E, 0xB5, 0xAE, 0xB1, 0x3B, 0x1A, 0x45, 0xDA, 0x33, 0x48,
};

template <class T>
void Put(std::vector<u8>& bytes, size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

FileSys::VirtualFile Npdm() {
    // Synthetic metadata only: one application, 64-bit instructions and a 39-bit address space.
    std::vector<u8> bytes(0x400);
    std::memcpy(bytes.data(), "META", 4);
    bytes[0xC] = 7;
    bytes[0xE] = 0x2C;
    Put(bytes, 0x1C, u32{0x100000});
    Put(bytes, 0x70, u32{0x80});
    Put(bytes, 0x74, u32{0x80});
    Put(bytes, 0x78, u32{0x100});
    Put(bytes, 0x7C, u32{0x300});
    std::memcpy(bytes.data() + 0x80, "ACI0", 4);
    Put(bytes, 0x90, TitleId);
    Put(bytes, 0xA0, u32{0x40}); // FAH relative to ACI0.
    Put(bytes, 0xA4, u32{0x1C});
    Put(bytes, 0xB0, u32{0x60}); // Kernel capabilities relative to ACI0.
    Put(bytes, 0xB4, u32{4});
    Put(bytes, 0xE0, u32{0x30043F7}); // Cores 0..3 and thread priorities 16..63.
    std::memcpy(bytes.data() + 0x300, "ACID", 4);
    Put(bytes, 0x320, u32{0x240}); // FAC relative to ACID.
    Put(bytes, 0x324, u32{0x2C});
    return std::make_shared<FileSys::VectorVfsFile>(std::move(bytes), "main.npdm");
}

FileSys::VirtualFile Nso(std::string name, bool supported_build = true) {
    const bool main = name == "main";
    Loader::NSOHeader header{};
    header.magic = Common::MakeMagic('N', 'S', 'O', '0');
    header.build_id = main ? MainBuildId : std::array<u8, 32>{};
    if (main && !supported_build) {
        header.build_id[0] ^= 1;
    }
    size_t file_offset = sizeof(header);
    for (size_t i = 0; i < header.segments.size(); ++i) {
        const u32 size = main && i == 2 ? MainDataSize : 0x1000;
        header.segments[i] = {static_cast<u32>(file_offset),
                              main && i == 2 ? MainDataLocation : static_cast<u32>(i * 0x1000),
                              size, {0}};
        header.segments_compressed_size[i] = size;
        file_offset += size;
    }
    header.segments[2].bss_size = main ? MainBssSize : 0x1000;
    // The large virtual gap costs no fixture bytes. No game instructions or data are included.
    std::vector<u8> bytes(file_offset);
    std::memcpy(bytes.data(), &header, sizeof(header));
    return std::make_shared<FileSys::VectorVfsFile>(std::move(bytes), std::move(name));
}
} // namespace

TEST_CASE("NSO layout reserves patched BSS before mapping later modules", "[core][nso_layout]") {
    const bool supported_build = GENERATE(true, false);
    CAPTURE(supported_build);
    ScopedSystemTestContext environment;
    const std::array paths{Common::FS::EdenPath::LoadDir, Common::FS::EdenPath::DumpDir,
                           Common::FS::EdenPath::SDMCDir};
    std::array<std::filesystem::path, paths.size()> old_paths;
    const auto root = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir);
    for (size_t i = 0; i < paths.size(); ++i) {
        old_paths[i] = Common::FS::GetEdenPath(paths[i]);
        const auto isolated = root / std::to_string(i);
        std::filesystem::create_directory(isolated);
        Common::FS::SetEdenPath(paths[i], isolated);
    }
    const auto old_scale = Settings::AppliedOcarinaRenderScale();
    const auto* old_profile = Core::OotUltrawide::AcceptedProfile();
    const auto old_dump = Settings::values.dump_nso.GetValue();
    const auto old_args = Settings::values.program_args.GetValue();
    const auto old_backend = Settings::values.cpu_backend.GetValue();
    const bool old_nce = Settings::IsNceEnabled();
    SCOPE_EXIT {
        for (size_t i = 0; i < paths.size(); ++i) {
            Common::FS::SetEdenPath(paths[i], old_paths[i]);
        }
        Settings::SetAppliedOcarinaRenderScale(old_scale);
        Core::OotUltrawide::ClearAcceptedProfile();
        if (old_profile) {
            Core::OotUltrawide::AcceptProfile(old_profile->aspect);
        }
        Settings::values.dump_nso.SetValue(old_dump);
        Settings::values.program_args.SetValue(old_args);
        Settings::values.cpu_backend.SetValue(old_backend);
        Settings::SetNceEnabled(old_nce);
    };
    Settings::values.cpu_backend.SetValue(Settings::CpuBackend::Dynarmic);
    Settings::SetNceEnabled(false);
    Settings::values.program_args.SetValue("");
    Core::System system;
    REQUIRE(system.Initialize());
    system.GetFileSystemController().CreateFactories(*system.GetFilesystem());
    auto& kernel = system.Kernel();
    Kernel::KThread bootstrap{kernel};
    REQUIRE(Kernel::KThread::InitializeDummyThread(system, &bootstrap, nullptr).IsSuccess());
    kernel.SetCurrentEmuThread(&bootstrap);
    kernel.Initialize();
    SCOPE_EXIT {
        system.CoreTiming().ClearPendingEvents();
        kernel.Shutdown();
        kernel.SetCurrentEmuThread(nullptr);
        kernel.UnregisterKernelObject(&bootstrap);
    };
    Kernel::KProcess process{kernel};
    Kernel::KAutoObject::Create(&process);
    SCOPE_EXIT { kernel.UnregisterKernelObject(&process); };

    const auto main = Nso("main", supported_build);
    const FileSys::PatchManager pm{TitleId, system.GetFileSystemController(),
                                   system.GetContentProvider()};
    const u64 main_size = MainDataLocation + MainDataSize + MainBssSize +
                          (supported_build ? Core::OotFpsPatches::ExtraBssSize : 0);
    static_assert((MainDataLocation + MainDataSize + MainBssSize) % 0x1000 == 0);

    // Exercise the real dry pass with a patch manager. It may size the extra page, but must not
    // apply executable patches, reset accepted rendering policy, or create NSO dumps.
    Settings::SetAppliedOcarinaRenderScale(Settings::NvidiaDlssQuality::Performance);
    Settings::values.dump_nso.SetValue(true);
    const auto dry_end = Loader::AppLoader_NSO::LoadModule(process, system, *main, 0, false,
                                                          false, pm);
    REQUIRE(dry_end.has_value());
    CHECK(*dry_end == main_size);
    CHECK(Settings::AppliedOcarinaRenderScale() == Settings::NvidiaDlssQuality::Performance);
    CHECK(Core::OotUltrawide::AcceptedProfile() == old_profile);
    CHECK(std::filesystem::is_empty(Common::FS::GetEdenPath(Common::FS::EdenPath::DumpDir)));
    Settings::values.dump_nso.SetValue(false);

    auto exefs = std::make_shared<FileSys::VectorVfsDirectory>(
        std::vector<FileSys::VirtualFile>{Npdm(), Nso("rtld"), main, Nso("subsdk0"), Nso("sdk")});
    Loader::AppLoader_DeconstructedRomDirectory loader{exefs};
    REQUIRE(loader.Load(process, system).first == Loader::ResultStatus::Success);
    Loader::AppLoader::Modules modules;
    REQUIRE(loader.ReadNSOModules(modules) == Loader::ResultStatus::Success);
    REQUIRE(modules.size() == 4);
    const u64 base = GetInteger(process.GetEntryPoint());
    CHECK(modules.at(base) == "rtld");
    CHECK(modules.at(base + SmallModuleSize) == "main");
    CHECK(modules.at(base + SmallModuleSize + main_size) == "subsdk0");
    const u64 sdk_base = base + 2 * SmallModuleSize + main_size;
    CHECK(modules.at(sdk_base) == "sdk");
    const u64 end = sdk_base + SmallModuleSize;
    auto& memory = process.GetMemory();
    // The original mismatched reservation leaves exactly this final SDK page unmapped while
    // Load still reports success. Assert mapping before reading so the failure is explicit.
    REQUIRE(memory.IsValidVirtualAddressRange(end - 0x1000, 0x1000));
    CHECK(memory.IsValidVirtualAddressRange(sdk_base, SmallModuleSize));
    CHECK(memory.Read8(end - 1) == 0);
    memory.Write8(end - 1, 0xA5);
    CHECK(memory.Read8(end - 1) == 0xA5);
    CHECK_FALSE(memory.IsValidVirtualAddress(end));
}
