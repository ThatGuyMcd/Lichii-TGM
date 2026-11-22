// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/alignment.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/random.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/vfs/vfs_offset.h"
#include "core/hle/kernel/code_set.h"
#include "core/hle/kernel/k_memory_manager.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/nro.h"
#include "core/memory.h"

#ifdef HAS_NCE
#include "core/arm/nce/patcher.h"
#endif

namespace Loader {

struct NroSegmentHeader {
    u32_le offset;
    u32_le size;
};
static_assert(sizeof(NroSegmentHeader) == 0x8, "NroSegmentHeader has incorrect size.");

struct NroHeader {
    INSERT_PADDING_BYTES(0x4);
    u32_le module_header_offset;
    u32 magic_ext1;
    u32 magic_ext2;
    u32_le magic;
    INSERT_PADDING_BYTES(0x4);
    u32_le file_size;
    INSERT_PADDING_BYTES(0x4);
    std::array<NroSegmentHeader, 3> segments; // Text, RoData, Data (in that order)
    u32_le bss_size;
    INSERT_PADDING_BYTES(0x44);
};
static_assert(sizeof(NroHeader) == 0x80, "NroHeader has incorrect size.");

struct ModHeader {
    u32_le magic;
    u32_le dynamic_offset;
    u32_le bss_start_offset;
    u32_le bss_end_offset;
    u32_le unwind_start_offset;
    u32_le unwind_end_offset;
    u32_le module_offset; // Offset to runtime-generated module object. typically equal to .bss base
};
static_assert(sizeof(ModHeader) == 0x1c, "ModHeader has incorrect size.");

struct AssetSection {
    u64_le offset;
    u64_le size;
};
static_assert(sizeof(AssetSection) == 0x10, "AssetSection has incorrect size.");

struct AssetHeader {
    u32_le magic;
    u32_le format_version;
    AssetSection icon;
    AssetSection nacp;
    AssetSection romfs;
};
static_assert(sizeof(AssetHeader) == 0x38, "AssetHeader has incorrect size.");

AppLoader_NRO::AppLoader_NRO(FileSys::VirtualFile file_) : AppLoader(std::move(file_)) {
    NroHeader nro_header{};
    if (file->ReadObject(&nro_header) != sizeof(NroHeader)) {
        return;
    }

    if (file->GetSize() >= nro_header.file_size + sizeof(AssetHeader)) {
        const u64 offset = nro_header.file_size;
        AssetHeader asset_header{};
        if (file->ReadObject(&asset_header, offset) != sizeof(AssetHeader)) {
            return;
        }

        if (asset_header.format_version != 0) {
            LOG_WARNING(Loader,
                        "NRO Asset Header has format {}, currently supported format is 0. If "
                        "strange glitches occur with metadata, check NRO assets.",
                        asset_header.format_version);
        }

        if (asset_header.magic != Common::MakeMagic('A', 'S', 'E', 'T')) {
            return;
        }

        if (asset_header.nacp.size > 0) {
            nacp = std::make_unique<FileSys::NACP>(std::make_shared<FileSys::OffsetVfsFile>(
                file, asset_header.nacp.size, offset + asset_header.nacp.offset, "Control.nacp"));
        }

        if (asset_header.romfs.size > 0) {
            romfs = std::make_shared<FileSys::OffsetVfsFile>(
                file, asset_header.romfs.size, offset + asset_header.romfs.offset, "game.romfs");
        }

        if (asset_header.icon.size > 0) {
            icon_data = file->ReadBytes(asset_header.icon.size, offset + asset_header.icon.offset);
        }
    }
}

AppLoader_NRO::~AppLoader_NRO() = default;

FileType AppLoader_NRO::IdentifyType(const FileSys::VirtualFile& nro_file) {
    // Read NSO header
    NroHeader nro_header{};
    if (sizeof(NroHeader) != nro_file->ReadObject(&nro_header)) {
        return FileType::Error;
    }
    if (nro_header.magic == Common::MakeMagic('N', 'R', 'O', '0')) {
        return FileType::NRO;
    }
    return FileType::Error;
}

bool AppLoader_NRO::IsHomebrew() {
    // Read NSO header
    NroHeader nro_header{};
    if (sizeof(NroHeader) != file->ReadObject(&nro_header)) {
        return false;
    }
    return nro_header.magic_ext1 == Common::MakeMagic('H', 'O', 'M', 'E') &&
           nro_header.magic_ext2 == Common::MakeMagic('B', 'R', 'E', 'W');
}

std::optional<NroLayout> ValidateNroLayout(const FileSys::VfsFile& file, std::string_view* reason) {
    constexpr u64 PageSize = Core::Memory::YUZU_PAGESIZE;
    const auto reject = [reason](std::string_view message) -> std::optional<NroLayout> {
        if (reason != nullptr) {
            *reason = message;
        }
        return std::nullopt;
    };
    NroHeader header{};
    if (file.ReadObject(&header) != sizeof(header) ||
        header.magic != Common::MakeMagic('N', 'R', 'O', '0')) {
        return reject("invalid NRO header");
    }
    const u64 file_size = header.file_size;
    if (file_size < sizeof(header) || file_size > file.GetSize()) {
        return reject("the declared program size is outside the file");
    }

    NroLayout layout{};
    u64 previous_end = 0;
    for (size_t i = 0; i < header.segments.size(); ++i) {
        const u64 offset = header.segments[i].offset;
        const u64 size = header.segments[i].size;
        if (size == 0) {
            layout.segments[i] = {previous_end, 0};
            continue;
        }
        if ((i == 0 && offset != 0) || offset % PageSize != 0 || offset < previous_end ||
            offset + size > file_size) {
            return reject("a program segment is outside the image, unaligned or overlapping");
        }
        const u64 page_size = Common::AlignUp(size, PageSize);
        layout.segments[i] = {offset, page_size};
        previous_end = offset + page_size;
    }

    const u64 file_image_size = Common::AlignUp(file_size, PageSize);
    u64 bss_size = Common::AlignUp(u64{header.bss_size}, PageSize);
    const u64 module_offset = header.module_header_offset;
    ModHeader module{};
    if (module_offset + sizeof(module) > file_size ||
        file.ReadObject(&module, static_cast<size_t>(module_offset)) != sizeof(module)) {
        layout.ignored_module_header = "the module header is outside the program or incomplete";
    } else if (module.magic == Common::MakeMagic('M', 'O', 'D', '0')) {
        if (module.bss_end_offset < module.bss_start_offset) {
            layout.ignored_module_header = "the module's .bss ends before it starts";
        } else {
            bss_size = Common::AlignUp(u64{module.bss_end_offset} - module.bss_start_offset,
                                       PageSize);
        }
    }

    layout.file_size = file_size;
    layout.image_size = file_image_size + bss_size;
    if (layout.image_size > std::numeric_limits<u32>::max()) {
        return reject("the program image and .bss exceed the code set size limit");
    }
    auto& data = layout.segments.back();
    if (data.size == 0) {
        data.offset = file_image_size;
    }
    data.size = layout.image_size - data.offset;
    return layout;
}

static bool LoadNroImpl(Core::System& system, Kernel::KProcess& process,
                       const FileSys::VfsFile& file) {
    std::string_view reason;
    const auto layout = ValidateNroLayout(file, &reason);
    if (!layout) {
        LOG_ERROR(Loader, "NRO not loaded: {}", reason);
        return false;
    }
    if (!layout->ignored_module_header.empty()) {
        LOG_WARNING(Loader, "NRO using .bss size from its header: {}", layout->ignored_module_header);
    }

    struct ConfigEntry {
        u32_le key;
        u32_le flags;
        u64_le value[2];
    };
    static_assert(sizeof(ConfigEntry) == 0x18);
    // AArch64 encoding for svc #0x7 (ExitProcess).
    constexpr u32 kSvcExitProcessInstruction = 0xD40000E1;
    constexpr size_t kNumEntries = 4; // MainThreadHandle, AppletType, Argv, EndOfList
    constexpr size_t kConfigTableSize = kNumEntries * sizeof(ConfigEntry);
    std::string argv_string;
    const auto program_args = Settings::values.program_args.GetValue();
    if (!program_args.empty()) {
        argv_string = "homebrew ";
        argv_string += program_args;
        argv_string.push_back('\0');
    }

    const u64 args_offset_in_image = layout->image_size;
    const u64 entries_and_argv =
        argv_string.empty()
            ? 0
            : Common::AlignUp(u64{kConfigTableSize} + argv_string.size(),
                              u64{Core::Memory::YUZU_PAGESIZE});
    const u64 total_size = args_offset_in_image + entries_and_argv;
    const auto metadata = FileSys::ProgramMetadata::GetDefault();
    const auto pool = static_cast<Kernel::KMemoryManager::Pool>(metadata.GetPoolPartition());
    const u64 pool_size = system.Kernel().MemoryManager().GetSize(pool);
    if (total_size > std::numeric_limits<u32>::max() || total_size > pool_size) {
        LOG_ERROR(Loader, "NRO not loaded: process cannot hold the {:#x}-byte image", total_size);
        return false;
    }

    Kernel::CodeSet codeset;
    for (size_t i = 0; i < layout->segments.size(); ++i) {
        const auto& segment = layout->segments[i];
        codeset.segments[i] = {segment.offset, segment.offset, static_cast<u32>(segment.size)};
    }
    codeset.DataSegment().size += static_cast<u32>(entries_and_argv);

    std::vector<u8> program_image(static_cast<size_t>(total_size));
    const auto file_size = static_cast<size_t>(layout->file_size);
    if (file.Read(program_image.data(), file_size) != file_size) {
        LOG_ERROR(Loader, "NRO not loaded: incomplete program image");
        return false;
    }

    std::optional<size_t> exit_process_offset_in_image;
    if (!argv_string.empty()) {
        const auto& code = codeset.CodeSegment();
        const size_t code_end = (std::min)(file_size, code.offset + code.size);
        for (size_t offset = code.offset; offset + sizeof(u32) <= code_end; offset += sizeof(u32)) {
            u32 instruction{};
            std::memcpy(&instruction, program_image.data() + offset, sizeof(instruction));
            if (instruction == kSvcExitProcessInstruction) {
                exit_process_offset_in_image = offset;
                break;
            }
        }
        if (!exit_process_offset_in_image) {
            LOG_WARNING(Loader,
                        "Unable to find svcExitProcess in NRO; returning from main may fault");
        }

    }
    size_t image_size = program_image.size();

#ifdef HAS_NCE
    const auto& code = codeset.CodeSegment();

    // NROs always have a 39-bit address space.
    Settings::SetNceEnabled(true);

    // Create NCE patcher
    Core::NCE::Patcher patch{};

    if (Settings::IsNceEnabled()) {
        // Patch SVCs and MRS calls in the guest code
        // We only support PostData patching for NROs.
        if (!patch.PatchText(program_image, code) ||
            patch.GetPatchMode() != Core::NCE::PatchMode::PostData) {
            LOG_ERROR(Loader, "NRO not loaded: unsupported native code patch layout");
            return false;
        }
        const u64 patch_size = patch.GetSectionSize();
        if (patch_size > std::numeric_limits<u32>::max() - image_size ||
            patch_size > pool_size - image_size) {
            LOG_ERROR(Loader, "NRO not loaded: native code patch exceeds process capacity");
            return false;
        }

        // Update patch section.
        auto& patch_segment = codeset.PatchSegment();
        patch_segment.addr = image_size;
        patch_segment.size = static_cast<u32>(patch_size);

        // Add patch section size to the module size.
        image_size += patch_segment.size;
    }
#endif

    // Enable direct memory mapping in case of NCE.
    const u64 fastmem_base = [&]() -> size_t {
        if (Settings::IsNceEnabled()) {
            auto& buffer = system.DeviceMemory().buffer;
            buffer.EnableDirectMappedAddress();
            return reinterpret_cast<u64>(buffer.VirtualBasePointer());
        }
        return 0;
    }();

    // TODO: this is bad form of ASLR, it sucks
    std::uintptr_t aslr_offset = ((::Settings::values.rng_seed_enabled.GetValue()
        ? ::Settings::values.rng_seed.GetValue() : Common::Random::Random64(0)) << 12) & 0xfff000;

    // Setup the process code layout
    if (process
            .LoadFromMetadata(system.Kernel(), metadata, image_size, fastmem_base, aslr_offset)
            .IsError()) {
        return false;
    }

    // Relocate code patch and copy to the program_image if running under NCE.
    // This needs to be after LoadFromMetadata so we can use the process entry point.
#ifdef HAS_NCE
    if (Settings::IsNceEnabled() &&
        !patch.RelocateAndCopy(process.GetEntryPoint(), code, program_image,
                               &process.GetPostHandlers())) {
        return false;
    }
#endif

    // Load codeset for current process
    codeset.memory = std::move(program_image);
    process.LoadModule(system.Kernel(), std::move(codeset), process.GetEntryPoint());
    if (!argv_string.empty()) {
        constexpr u32 kEntryEndOfList = 0;
        constexpr u32 kEntryMainThreadHandle = 1;
        constexpr u32 kEntryArgv = 5;
        constexpr u32 kEntryAppletType = 7;
        constexpr u32 kAppletTypeApplication = 0;

        const u64 base = GetInteger(process.GetEntryPoint());
        const u64 config_addr = base + args_offset_in_image;
        const u64 argv_addr = config_addr + kConfigTableSize;

        const ConfigEntry entries[kNumEntries] = {
            {kEntryMainThreadHandle, 0, {0, 0}}, // Value[0] patched in Run()
            {kEntryAppletType,       0, {kAppletTypeApplication, 0}},
            {kEntryArgv,             0, {0, argv_addr}},
            {kEntryEndOfList,        0, {0, 0}},
        };
        process.GetMemory().WriteBlock(Common::ProcessAddress{config_addr}, entries, sizeof(entries));
        process.GetMemory().WriteBlock(Common::ProcessAddress{argv_addr}, argv_string.data(), argv_string.size());
        constexpr size_t kMainThreadHandleValueOffset = offsetof(ConfigEntry, value);
        process.SetArgPointer(Kernel::KProcessAddress{config_addr});
        if (exit_process_offset_in_image) {
            process.SetArgReturnAddress(Kernel::KProcessAddress{base + *exit_process_offset_in_image});
        }
        process.SetMainThreadHandleAddr(Kernel::KProcessAddress{config_addr + kMainThreadHandleValueOffset});
    }

    return true;
}

bool AppLoader_NRO::LoadNro(Core::System& system, Kernel::KProcess& process,
                            const FileSys::VfsFile& nro_file) {
    return LoadNroImpl(system, process, nro_file);
}

AppLoader_NRO::LoadResult AppLoader_NRO::Load(Kernel::KProcess& process, Core::System& system) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }

    if (!LoadNro(system, process, *file)) {
        return {ResultStatus::ErrorLoadingNRO, {}};
    }

    u64 program_id{};
    ReadProgramId(program_id);
    system.GetFileSystemController().RegisterProcess(
        process.GetProcessId(), program_id,
        std::make_unique<FileSys::RomFSFactory>(*this, system.GetContentProvider(),
                                                system.GetFileSystemController()));

    is_loaded = true;
    return {ResultStatus::Success, LoadParameters{Kernel::KThread::DefaultThreadPriority,
                                                  Core::Memory::DEFAULT_STACK_SIZE}};
}

ResultStatus AppLoader_NRO::ReadIcon(std::vector<u8>& buffer) {
    if (icon_data.empty()) {
        return ResultStatus::ErrorNoIcon;
    }

    buffer = icon_data;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadProgramId(u64& out_program_id) {
    if (nacp == nullptr) {
        return ResultStatus::ErrorNoControl;
    }

    out_program_id = nacp->GetTitleId();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadRomFS(FileSys::VirtualFile& dir) {
    if (romfs == nullptr) {
        return ResultStatus::ErrorNoRomFS;
    }

    dir = romfs;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadTitle(std::string& title) {
    if (nacp == nullptr) {
        return ResultStatus::ErrorNoControl;
    }

    title = nacp->GetApplicationName();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadControlData(FileSys::NACP& control) {
    if (nacp == nullptr) {
        return ResultStatus::ErrorNoControl;
    }

    control = *nacp;
    return ResultStatus::Success;
}

bool AppLoader_NRO::IsRomFSUpdatable() const {
    return false;
}

} // namespace Loader
