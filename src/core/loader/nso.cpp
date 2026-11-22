// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include "common/common_funcs.h"
#include "common/game_presets.h"
#include "common/hex_util.h"
#include "common/logging.h"
#include "common/lz4_compression.h"
#include "common/zbic_compression.h"
#include "common/settings.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/arm/dynarmic/window_trace.h"
#include "core/file_sys/patch_manager.h"
#include "core/hle/kernel/code_set.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/loader/nso.h"
#include "core/memory.h"
#include "core/oot_zstd_hook.h"

#ifdef HAS_NCE
#include "core/arm/nce/patcher.h"
#endif

namespace Loader {
namespace {
struct MODHeader {
    u32_le magic;
    u32_le dynamic_offset;
    u32_le bss_start_offset;
    u32_le bss_end_offset;
    u32_le eh_frame_hdr_start_offset;
    u32_le eh_frame_hdr_end_offset;
    u32_le module_offset; // Offset to runtime-generated module object. typically equal to .bss base
};
static_assert(sizeof(MODHeader) == 0x1c, "MODHeader has incorrect size.");

constexpr u32 PageAlignSize(u32 size) {
    return static_cast<u32>((size + Core::Memory::YUZU_PAGEMASK) & ~Core::Memory::YUZU_PAGEMASK);
}
} // Anonymous namespace

bool NSOHeader::IsSegmentCompressed(size_t segment_num) const {
    ASSERT_MSG(segment_num < 3, "Invalid segment {}", segment_num);
    return ((flags >> segment_num) & 1) != 0;
}

AppLoader_NSO::AppLoader_NSO(FileSys::VirtualFile file_) : AppLoader(std::move(file_)) {}

FileType AppLoader_NSO::IdentifyType(const FileSys::VirtualFile& in_file) {
    u32 magic = 0;
    if (in_file->ReadObject(&magic) != sizeof(magic)) {
        return FileType::Error;
    }

    if (Common::MakeMagic('N', 'S', 'O', '0') != magic) {
        return FileType::Error;
    }

    return FileType::NSO;
}

std::optional<VAddr> AppLoader_NSO::LoadModule(Kernel::KProcess& process, Core::System& system, const FileSys::VfsFile& nso_file, VAddr load_base, bool should_pass_arguments, bool load_into_process, std::optional<FileSys::PatchManager> pm, std::vector<Core::NCE::Patcher>* patches, s32 patch_index) {
    if (nso_file.GetSize() < sizeof(NSOHeader))
        return std::nullopt;
    NSOHeader nso_header{};
    if (sizeof(NSOHeader) != nso_file.ReadObject(&nso_header))
        return std::nullopt;
    if (nso_header.magic != Common::MakeMagic('N', 'S', 'O', '0'))
        return std::nullopt;
    if (nso_header.segments.empty())
        return std::nullopt;

    // Allocate some space at the beginning if we are patching in PreText mode.
    const size_t module_start = [&]() -> size_t {
#ifdef HAS_NCE
        if (patches && load_into_process) {
            auto* patch = &patches->operator[](patch_index);
            if (patch->GetPatchMode() == Core::NCE::PatchMode::PreText) {
                return patch->GetSectionSize();
            } else if (patch->GetPatchMode() == Core::NCE::PatchMode::Split) {
                return patch->GetPreSectionSize();
            }
        }
#endif
        return 0;
    }();

    u64 segments_end = 0;
    const u64 file_size = nso_file.GetSize();
    const bool zbic_compressed = nso_header.IsZBICCompressed();
    size_t largest_stored_segment = 0;
    for (size_t i = 0; i < nso_header.segments.size(); ++i) {
        const auto& segment = nso_header.segments[i];
        const u64 stored_size = nso_header.segments_compressed_size[i];
        if (segment.offset > file_size || stored_size > file_size - segment.offset) {
            LOG_ERROR(Loader, "{}: segment {} is past the end of the file", nso_file.GetName(), i);
            return std::nullopt;
        }
        if (nso_header.IsSegmentCompressed(i)) {
            // LZ4 can emit at most 255 bytes for each source byte.
            if (!zbic_compressed &&
                (stored_size > (std::numeric_limits<int>::max)() ||
                 segment.size > (std::numeric_limits<int>::max)() ||
                 segment.size > stored_size * 255)) {
                LOG_ERROR(Loader, "{}: segment {} has an invalid LZ4 size", nso_file.GetName(), i);
                return std::nullopt;
            }
            if (segment.size != 0 && stored_size == 0) {
                LOG_ERROR(Loader, "{}: segment {} has no compressed data", nso_file.GetName(), i);
                return std::nullopt;
            }
            if (segment.size != 0) {
                largest_stored_segment =
                    (std::max)(largest_stored_segment, static_cast<size_t>(stored_size));
            }
        }
        segments_end = (std::max)(segments_end, u64{segment.location} + segment.size);
        if (!nso_header.IsSegmentCompressed(i) &&
            segment.size > nso_header.segments_compressed_size[i]) {
            LOG_ERROR(Loader, "{}: stored segment {} is larger than its data", nso_file.GetName(), i);
            return std::nullopt;
        }
    }
    const bool pass_arguments =
        should_pass_arguments && !Settings::values.program_args.GetValue().empty();
    if (pass_arguments && Settings::values.program_args.GetValue().size() >
                              NSO_ARGUMENT_DATA_ALLOCATION_SIZE - sizeof(NSOArgumentHeader)) {
        LOG_ERROR(Loader, "{}: arguments exceed the reserved block", nso_file.GetName());
        return std::nullopt;
    }
    const u64 bss_size = u64{nso_header.segments[2].bss_size} +
                         (pm ? pm->AdditionalNSOBssSize(nso_header, nso_file.GetName()) : 0);
    const u64 post_patch_size = [&]() -> u64 {
#ifdef HAS_NCE
        if (patches && load_into_process) {
            const auto& patch = patches->operator[](patch_index);
            if (patch.GetPatchMode() != Core::NCE::PatchMode::PreText) {
                return patch.GetSectionSize();
            }
        }
#endif
        return 0;
    }();
    constexpr u64 MaxImage = (std::numeric_limits<u32>::max)();
    if (module_start > MaxImage || post_patch_size > MaxImage) {
        return std::nullopt;
    }
    // Count TGM's additional OOT BSS in both passes before narrowing or allocating. Applying
    // executable patches remains restricted to the real load below.
    const u64 aligned_extent =
        (u64{module_start} + segments_end +
         (pass_arguments ? NSO_ARGUMENT_DATA_ALLOCATION_SIZE : 0) + bss_size +
         Core::Memory::YUZU_PAGEMASK) & ~u64{Core::Memory::YUZU_PAGEMASK};
    if (aligned_extent + post_patch_size > MaxImage) {
        LOG_ERROR(Loader, "{}: module extent exceeds a 32-bit image", nso_file.GetName());
        return std::nullopt;
    }
    nso_header.segments[2].bss_size = static_cast<u32>(bss_size);
    // Build program image directly in codeset memory :)
    Kernel::CodeSet codeset;
    codeset.memory.resize(module_start + segments_end);
    {
        std::vector<u8> compressed_data(largest_stored_segment);
        for (std::size_t i = 0; i < nso_header.segments.size(); ++i) {
            const auto& segment = nso_header.segments[i];
            const size_t image_offset = module_start + segment.location;
            const u32 segment_size = segment.size;
            codeset.segments[i].addr = image_offset;
            codeset.segments[i].offset = image_offset;
            codeset.segments[i].size = segment_size;
            if (segment_size == 0) {
                continue;
            }

            const auto destination = std::span{codeset.memory}.subspan(image_offset, segment_size);
            const bool compressed = nso_header.IsSegmentCompressed(i);
            const u32 stored_size = nso_header.segments_compressed_size[i];
            const auto input = compressed ? std::span{compressed_data}.first(stored_size)
                                          : destination;
            if (nso_file.Read(input.data(), input.size(), segment.offset) != input.size()) {
                LOG_ERROR(Loader, "{}: cannot read segment {}", nso_file.GetName(), i);
                return std::nullopt;
            }
            if (!compressed) {
                continue;
            }

            if (zbic_compressed) {
                const int decoded = Common::Compression::DecompressDataZBIC(destination, input);
                if (decoded <= 0 || static_cast<u32>(decoded) > segment_size) {
                    LOG_ERROR(Loader, "{}: segment {} failed ZBIC decompression", nso_file.GetName(),
                              i);
                    return std::nullopt;
                }
                if (static_cast<u32>(decoded) != segment_size) {
                    LOG_WARNING(Loader, "{}: segment {} decompressed to {:#x} of {:#x} bytes",
                                nso_file.GetName(), i, decoded, segment_size);
                    std::ranges::fill(destination.subspan(static_cast<size_t>(decoded)), u8{0});
                }
            } else {
                const int decoded = Common::Compression::DecompressDataLZ4(
                    destination.data(), segment_size, input.data(), stored_size);
                if (decoded != static_cast<int>(segment_size)) {
                    LOG_ERROR(Loader, "{}: segment {} decompressed to {} of {} bytes",
                              nso_file.GetName(), i, decoded, segment_size);
                    return std::nullopt;
                }
            }
        }
    }

    if (pass_arguments) {
        const auto arg_data{Settings::values.program_args.GetValue()};

        codeset.DataSegment().size += NSO_ARGUMENT_DATA_ALLOCATION_SIZE;
        NSOArgumentHeader args_header{NSO_ARGUMENT_DATA_ALLOCATION_SIZE, static_cast<u32_le>(arg_data.size()), {}};
        const auto end_offset = codeset.memory.size();
        codeset.memory.resize(u32(codeset.memory.size()) + NSO_ARGUMENT_DATA_ALLOCATION_SIZE);
        std::memcpy(codeset.memory.data() + end_offset, &args_header, sizeof(NSOArgumentHeader));
        std::memcpy(codeset.memory.data() + end_offset + sizeof(NSOArgumentHeader), arg_data.data(), arg_data.size());
    }

    codeset.DataSegment().size += nso_header.segments[2].bss_size;
    u32 image_size = PageAlignSize(u32(codeset.memory.size()) + nso_header.segments[2].bss_size);
    codeset.memory.resize(image_size);

    for (std::size_t i = 0; i < nso_header.segments.size(); ++i) {
        codeset.segments[i].size = PageAlignSize(codeset.segments[i].size);
    }

    // Sizing needs the patched BSS extent, but must not apply or dump executable patches.
    const auto name = nso_file.GetName();
    if (load_into_process && pm &&
        (pm->HasNSOPatch(nso_header.build_id, name) || Settings::values.dump_nso)) {
        std::span<u8> patchable_section(codeset.memory.data() + module_start, codeset.memory.size() - module_start);
        std::vector<u8> pi_header(sizeof(NSOHeader) + patchable_section.size());
        std::memcpy(pi_header.data(), &nso_header, sizeof(NSOHeader));
        std::memcpy(pi_header.data() + sizeof(NSOHeader), patchable_section.data(),
                    patchable_section.size());

        pi_header = pm->PatchNSO(pi_header, name);

        std::copy(pi_header.begin() + sizeof(NSOHeader), pi_header.end(), patchable_section.data());
    }

    // Mario Kart World selects a 1- or 2-vblank frame mode in its main loop. Force its native
    // 30 FPS mode, or let it choose dynamically. In dynamic mode the game keeps a time account
    // (frame cost - interval * 16.67 ms) and advances 1 or 2 steps per frame, so it stays in real
    // time as long as the cost it reads is the real frame time: use the whole frame time and skip
    // the game's own wait for the second vblank, or every 2-step frame reads as 33 ms and the game
    // never leaves 30 FPS (found by SerkiGohan). Presents are then paced with interval 1.
    // Match the instruction sequences instead of relying on a title-version-specific address.
    const bool mkw_dynamic = Settings::values.mkw_dynamic_fps.GetValue();
    bool mkw_dynamic_fps_patch_applied = false;
    if ((mkw_dynamic || Settings::values.mkw_30fps.GetValue()) && name == "main" &&
        process.GetProgramId() == Settings::MarioKartWorldProgramId) {
        constexpr u32 ModeLoadInsn = 0xB9421268; // ldr w8, [x19, #0x210]
        const u32 mode = mkw_dynamic ? 0U : 2U;
        const u32 mode_instruction = 0x52800008U | (mode << 5); // mov w8, #mode
        const auto& text = nso_header.segments[0];
        u8* const code = codeset.memory.data() + module_start + text.location;
        const size_t count = text.size / sizeof(u32);
        const auto word = [code](size_t index) {
            u32 value;
            std::memcpy(&value, code + index * sizeof(u32), sizeof(value));
            return value;
        };
        size_t mode_site = 0;
        size_t mode_matches = 0;
        for (size_t i = 2; i + 8 < count; ++i) {
            if (word(i) != ModeLoadInsn || word(i - 2) != 0x6B01013F || // cmp w9, w1
                (word(i - 1) & 0xFF00001F) != 0x54000001 ||             // b.ne
                (word(i + 1) & 0xFF00001F) != 0x35000008) {             // cbnz w8
                continue;
            }
            for (size_t j = i + 2; j < i + 8; ++j) {
                if (word(j) == 0x52A84E08) { // mov w8, #60.0f
                    mode_site = i;
                    ++mode_matches;
                    break;
                }
            }
        }

        size_t frame_time_site = 0;
        size_t frame_time_matches = 0;
        if (mode_matches == 1 && mkw_dynamic) {
            for (size_t i = mode_site + 1; i < mode_site + 32 && i < count; ++i) {
                if ((word(i) & 0xFFFFFFE0) == 0xF9409660) { // ldr xN, [x19, #0x128]
                    frame_time_site = i;
                    ++frame_time_matches;
                }
            }
        }

        // The vsync function waits until 0.99 * interval / 60 s after the frame start:
        // ldr w8, [x19, #0x88] ; cmp w8, #2 ; b.lo <done> ; ucvtf ; mov w8, #60.0f
        size_t vblank_wait_site = 0;
        size_t vblank_wait_matches = 0;
        if (mkw_dynamic) {
            for (size_t i = 0; i + 4 < count; ++i) {
                if (word(i) == 0xB9408A68 && word(i + 1) == 0x7100091F &&
                    (word(i + 2) & 0xFF00001F) == 0x54000003 && word(i + 4) == 0x52A84E08) {
                    vblank_wait_site = i + 2;
                    ++vblank_wait_matches;
                }
            }
        }

        if (mode_matches == 1 &&
            (!mkw_dynamic || (frame_time_matches == 1 && vblank_wait_matches == 1))) {
            std::memcpy(code + mode_site * sizeof(u32), &mode_instruction,
                        sizeof(mode_instruction));
            if (mkw_dynamic) {
                const u32 whole_frame_time = 0xF9407660 | (word(frame_time_site) & 0x1F);
                std::memcpy(code + frame_time_site * sizeof(u32), &whole_frame_time,
                            sizeof(whole_frame_time));
                // b.lo -> b to the same target.
                const s32 offset = static_cast<s32>(word(vblank_wait_site) << 8) >> 13;
                const u32 branch = 0x14000000U | (static_cast<u32>(offset) & 0x03FFFFFFU);
                std::memcpy(code + vblank_wait_site * sizeof(u32), &branch, sizeof(branch));
                mkw_dynamic_fps_patch_applied = true;
            }
            LOG_INFO(Loader, "Mario Kart World: {} FPS mode enabled (main+{:#x})",
                     mkw_dynamic ? "dynamic" : "30",
                     text.location + mode_site * sizeof(u32));
        } else {
            LOG_WARNING(Loader,
                        "Mario Kart World FPS patch skipped (mode matches {}, frame-time matches "
                        "{}, vblank-wait matches {})",
                        mode_matches, frame_time_matches, vblank_wait_matches);
        }
    }

#ifdef HAS_NCE
    // If we are computing the process code layout and using nce backend, patch.
    const auto& code = codeset.CodeSegment();
    auto* patch = patches ? &patches->operator[](patch_index) : nullptr;
    if (patch && !load_into_process) {
        //Set module ID using build_id from the NSO header
        patch->SetModuleID(nso_header.build_id);
        // Patch SVCs and MRS calls in the guest code
        while (!patch->PatchText(codeset.memory, code)) {
            patch = &patches->emplace_back();
            patch->SetModuleID(nso_header.build_id);  // In case the patcher is changed for big modules, the new patcher should also have the build_id
        }
    } else if (patch) {
        // Relocate code patch and copy to the program image.
        // Save size before RelocateAndCopy (which may resize)
        const size_t size_before_relocate = codeset.memory.size();
        if (patch->RelocateAndCopy(load_base, code, codeset.memory, &process.GetPostHandlers())) {
            // Update patch section.
            auto& patch_segment = codeset.PatchSegment();
            auto& post_patch_segment = codeset.PostPatchSegment();
            const auto patch_mode = patch->GetPatchMode();
            if (patch_mode == Core::NCE::PatchMode::PreText) {
                patch_segment.addr = 0;
                patch_segment.size = static_cast<u32>(patch->GetSectionSize());
            } else if (patch_mode == Core::NCE::PatchMode::Split) {
                // For Split-mode, we are using pre-patch buffer at start, post-patch buffer at end
                patch_segment.addr = 0;
                patch_segment.size = static_cast<u32>(patch->GetPreSectionSize());
                post_patch_segment.addr = size_before_relocate;
                post_patch_segment.size = static_cast<u32>(patch->GetSectionSize());
            } else {
                patch_segment.addr = image_size;
                patch_segment.size = static_cast<u32>(patch->GetSectionSize());
            }
        }

        // Refresh image_size to take account the patch section if it was added by RelocateAndCopy
        image_size = static_cast<u32>(codeset.memory.size());
    }
#endif

    // If we aren't actually loading (i.e. just computing the process code layout), we are done
    if (!load_into_process) {
#ifdef HAS_NCE
        // Ok, so for Split mode, we need to account for pre-patch and post-patch space
        // which will be added during RelocateAndCopy in the second pass. Where it crashed
        // in Android Studio at PreText. May be a better way. Works for now.
        if (patch && patch->GetPatchMode() == Core::NCE::PatchMode::Split) {
            return load_base + patch->GetPreSectionSize() + image_size + patch->GetSectionSize();
        } else if (patch && patch->GetPatchMode() == Core::NCE::PatchMode::PreText) {
            return load_base + patch->GetSectionSize() + image_size;
        } else if (patch && patch->GetPatchMode() == Core::NCE::PatchMode::PostData) {
            return load_base + image_size + patch->GetSectionSize();
        }
#endif
        return load_base + image_size;
    }

    // Apply cheats if they exist and the program has a valid title ID
    if (pm) {
        // TODO(Maufeat): Check if there is a better way to check
        if (name == "main")
            system.SetApplicationProcessBuildID(nso_header.build_id);

        const auto cheats = pm->CreateCheatList(nso_header.build_id);
        if (!cheats.empty()) {
            system.RegisterCheatList(cheats, nso_header.build_id, load_base, image_size);
        }
    }

    // Load codeset for current process
    if (name == "sdk") {
        const bool matching_sdk =
            Core::WindowTrace::Enabled() && process.GetProgramId() == Core::WindowTrace::ProgramId &&
            Common::HexToString(std::span{nso_header.build_id}.first(20)) ==
                "41D4DE4A3B78214AD20C896960DF0E5EA0ED595F";
        Core::WindowTrace::sessions.Reset(&process, matching_sdk ? load_base : 0);
        if (Core::WindowTrace::Enabled()) {
            const auto session = Core::WindowTrace::sessions.Find(&process);
            LOG_INFO(Loader, "Star Fox window trace: matching SDK={} base={:#x} session={}",
                     matching_sdk, matching_sdk ? load_base : 0, session ? session->id : 0);
        }
    }
    Core::OotZstd::PatchModule(process.GetProgramId(), nso_header.build_id, name, codeset.memory,
                               load_base);
    LOG_INFO(Loader, "Module {} loaded at {:#x} (size {:#x})", nso_file.GetName(), load_base, image_size);
    if (nso_file.GetName() == "main") {
        system.SetApplicationMainBase(load_base);
    }
    process.LoadModule(system.Kernel(), std::move(codeset), load_base);
    if (name == "main" && process.GetProgramId() == Settings::MarioKartWorldProgramId) {
        process.SetMkwDynamicFpsPatchApplied(mkw_dynamic_fps_patch_applied);
    }
    return load_base + image_size;
}

AppLoader_NSO::LoadResult AppLoader_NSO::Load(Kernel::KProcess& process, Core::System& system) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }

    modules.clear();

    // Load module
    const VAddr base_address = GetInteger(process.GetEntryPoint());
    if (!LoadModule(process, system, *file, base_address, true, true)) {
        return {ResultStatus::ErrorLoadingNSO, {}};
    }

    modules.insert_or_assign(base_address, file->GetName());
    LOG_DEBUG(Loader, "loaded module {} @ {:#x}", file->GetName(), base_address);

    is_loaded = true;
    return {ResultStatus::Success, LoadParameters{Kernel::KThread::DefaultThreadPriority,
                                                  Core::Memory::DEFAULT_STACK_SIZE}};
}

ResultStatus AppLoader_NSO::ReadNSOModules(Modules& out_modules) {
    out_modules = this->modules;
    return ResultStatus::Success;
}

} // namespace Loader
