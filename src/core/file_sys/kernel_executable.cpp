// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <limits>

#include "common/alignment.h"
#include "common/logging.h"
#include "common/string_util.h"
#include "core/file_sys/kernel_executable.h"
#include "core/file_sys/vfs/vfs_offset.h"
#include "core/loader/loader.h"

namespace FileSys {

constexpr u32 INI_MAX_KIPS = 0x50;

namespace {
bool DecompressBLZ(std::vector<u8>& data, u64 section_size) {
    if (data.size() < 0xC)
        return {};

    const auto data_size = data.size() - 0xC;

    u32 compressed_size{};
    u32 init_index{};
    u32 additional_size{};
    std::memcpy(&compressed_size, data.data() + data_size, sizeof(u32));
    std::memcpy(&init_index, data.data() + data_size + 0x4, sizeof(u32));
    std::memcpy(&additional_size, data.data() + data_size + 0x8, sizeof(u32));

    if (compressed_size > data.size() || init_index > compressed_size) {
        return false;
    }
    // BLZ copies at most 144 bytes from 17 input bytes. Check the footer's expansion in 64 bits.
    const u64 out_size = u64{compressed_size} + additional_size;
    if (out_size > u64{compressed_size} * 9 + 144) {
        return false;
    }

    const auto start_offset = data.size() - compressed_size;
    if (out_size + start_offset > section_size) {
        return false;
    }
    data.resize(out_size + start_offset);

    std::size_t index = compressed_size - init_index;
    std::size_t out_index = out_size;

    while (out_index > 0) {
        if (index < 1) {
            return false;
        }
        --index;
        auto control = data[index + start_offset];
        for (size_t i = 0; i < 8; ++i) {
            if (((control << i) & 0x80) > 0) {
                if (index < 2) {
                    return false;
                }
                index -= 2;
                std::size_t segment_offset =
                    data[index + start_offset] | data[index + start_offset + 1] << 8;
                std::size_t segment_size = ((segment_offset >> 12) & 0xF) + 3;
                segment_offset &= 0xFFF;
                segment_offset += 3;

                if (out_index < segment_size)
                    segment_size = out_index;

                if (out_index < segment_size) {
                    return false;
                }

                out_index -= segment_size;

                for (size_t j = 0; j < segment_size; ++j) {
                    if (out_index + j + segment_offset + start_offset >= data.size()) {
                        return false;
                    }
                    data[out_index + j + start_offset] =
                        data[out_index + j + segment_offset + start_offset];
                }
            } else {
                if (out_index < 1 || index < 1) {
                    return false;
                }
                --out_index;
                --index;
                data[out_index + start_offset] = data[index + start_offset];
            }

            if (out_index == 0)
                break;
        }
    }

    return true;
}

bool ValidateKipLayout(const KIPHeader& header, std::array<KIPSegment, 3>& segments,
                       u64& image_size) {
    constexpr u64 PageSize = 0x1000;
    const u64 text_offset = header.sections[0].offset;
    const u64 text_size = Common::AlignUp(u64{header.sections[0].decompressed_size}, PageSize);
    const u64 ro_offset = header.sections[1].offset;
    const u64 ro_size = Common::AlignUp(u64{header.sections[1].decompressed_size}, PageSize);
    const u64 data_offset = header.sections[2].offset;
    const u64 data_size = header.sections[2].decompressed_size;
    const u64 bss_offset = header.sections[3].offset;
    const u64 bss_size = header.sections[3].decompressed_size;

    if (text_offset != 0 || ro_offset % PageSize != 0 || data_offset % PageSize != 0 ||
        text_size > ro_offset || ro_offset + ro_size > data_offset ||
        (bss_size != 0 && data_offset + data_size > bss_offset)) {
        return false;
    }
    const u64 end = Common::AlignUp(bss_size != 0 ? bss_offset + bss_size
                                                : data_offset + data_size,
                                    PageSize);
    if (end == 0 || end > std::numeric_limits<u32>::max()) {
        return false;
    }
    segments = {{{0, text_size}, {ro_offset, ro_size}, {data_offset, end - data_offset}}};
    image_size = end;
    return true;
}
} // Anonymous namespace

KIP::KIP(const VirtualFile& file) : status(Loader::ResultStatus::Success) {
    if (file == nullptr) {
        status = Loader::ResultStatus::ErrorNullFile;
        return;
    }

    if (file->GetSize() < sizeof(KIPHeader) || file->ReadObject(&header) != sizeof(KIPHeader)) {
        status = Loader::ResultStatus::ErrorBadKIPHeader;
        return;
    }

    if (header.magic != Common::MakeMagic('K', 'I', 'P', '1')) {
        status = Loader::ResultStatus::ErrorBadKIPHeader;
        return;
    }

    u64 record_size = sizeof(KIPHeader);
    for (const auto& section : header.sections) {
        record_size += section.compressed_size;
    }
    if (record_size > file->GetSize()) {
        status = Loader::ResultStatus::ErrorBadKIPHeader;
        return;
    }
    stored_size = record_size;

    if (!ValidateKipLayout(header, segments, image_size)) {
        status = Loader::ResultStatus::ErrorBadKIPHeader;
        return;
    }
    const auto reject = [this](Loader::ResultStatus result) {
        status = result;
        decompressed_sections = {};
        segments = {};
        image_size = 0;
    };
    u64 offset = sizeof(KIPHeader);
    for (size_t i = 0; i < decompressed_sections.size(); ++i) {
        const auto& header_section = header.sections[i];
        const u32 size = header_section.compressed_size;
        const bool compressed = (header.flags & (1U << i)) != 0;
        if (!compressed && size > header_section.decompressed_size) {
            reject(Loader::ResultStatus::ErrorBadKIPHeader);
            return;
        }
        auto& section = decompressed_sections[i];
        section = file->ReadBytes(size, static_cast<size_t>(offset));
        offset += size;
        if (section.size() != size) {
            reject(Loader::ResultStatus::ErrorBadKIPHeader);
            return;
        }
        if (compressed && !section.empty() &&
            !DecompressBLZ(section, header_section.decompressed_size)) {
            reject(Loader::ResultStatus::ErrorBLZDecompressionFailed);
            return;
        }
    }
}

Loader::ResultStatus KIP::GetStatus() const {
    return status;
}

std::string KIP::GetName() const {
    return Common::StringFromFixedZeroTerminatedBuffer(header.name.data(), header.name.size());
}

u64 KIP::GetTitleID() const {
    return header.title_id;
}

std::vector<u8> KIP::GetSectionDecompressed(u8 index) const {
    return index < decompressed_sections.size() ? decompressed_sections[index] : std::vector<u8>{};
}

const std::array<KIPSegment, 3>& KIP::GetSegments() const {
    return segments;
}

u64 KIP::GetImageSize() const {
    return image_size;
}

std::optional<u64> KIP::GetStoredSize() const {
    return stored_size;
}

bool KIP::Is64Bit() const {
    return (header.flags & 0x8) != 0;
}

bool KIP::Is39BitAddressSpace() const {
    return (header.flags & 0x10) != 0;
}

bool KIP::IsService() const {
    return (header.flags & 0x20) != 0;
}

std::vector<u32> KIP::GetKernelCapabilities() const {
    return std::vector<u32>(header.capabilities.begin(), header.capabilities.end());
}

s32 KIP::GetMainThreadPriority() const {
    return static_cast<s32>(header.main_thread_priority);
}

u32 KIP::GetMainThreadStackSize() const {
    return header.sections[1].attribute;
}

u32 KIP::GetMainThreadCpuCore() const {
    return header.default_core;
}

std::span<const u8> KIP::GetTextSection() const {
    return decompressed_sections[0];
}

std::span<const u8> KIP::GetRODataSection() const {
    return decompressed_sections[1];
}

std::span<const u8> KIP::GetDataSection() const {
    return decompressed_sections[2];
}

u32 KIP::GetTextOffset() const {
    return header.sections[0].offset;
}

u32 KIP::GetRODataOffset() const {
    return header.sections[1].offset;
}

u32 KIP::GetDataOffset() const {
    return header.sections[2].offset;
}

u32 KIP::GetBSSSize() const {
    return header.sections[3].decompressed_size;
}

u32 KIP::GetBSSOffset() const {
    return header.sections[3].offset;
}

INI::INI(const VirtualFile& file) : status(Loader::ResultStatus::Success) {
    if (file == nullptr || file->GetSize() < sizeof(INIHeader) ||
        file->ReadObject(&header) != sizeof(INIHeader)) {
        status = Loader::ResultStatus::ErrorBadINIHeader;
        return;
    }

    if (header.magic != Common::MakeMagic('I', 'N', 'I', '1')) {
        status = Loader::ResultStatus::ErrorBadINIHeader;
        return;
    }

    if (header.kip_count > INI_MAX_KIPS) {
        status = Loader::ResultStatus::ErrorINITooManyKIPs;
        return;
    }

    const auto file_size = file->GetSize();
    u64 offset = sizeof(INIHeader);
    for (size_t i = 0; i < header.kip_count && offset < file_size; ++i) {
        const auto kip_file =
            std::make_shared<OffsetVfsFile>(file, file_size - offset, offset);
        KIP kip(kip_file);
        const auto size = kip.GetStoredSize();
        if (!size) {
            break;
        }
        offset += *size;
        if (kip.GetStatus() != Loader::ResultStatus::Success) {
            LOG_WARNING(Loader, "INI1: skipped KIP {} ({}): {}", i, kip.GetName(),
                        Loader::GetResultStatusString(kip.GetStatus()));
            continue;
        }
        kips.push_back(std::move(kip));
    }
}

Loader::ResultStatus INI::GetStatus() const {
    return status;
}

const std::vector<KIP>& INI::GetKIPs() const {
    return kips;
}

} // namespace FileSys
