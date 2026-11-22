// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

#include "common/hex_util.h"
#include "common/logging.h"
#include <ranges>
#include "core/crypto/aes_util.h"
#include "core/crypto/ctr_encryption_layer.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/partition_filesystem.h"
#include "core/file_sys/vfs/vfs_offset.h"
#include "core/loader/loader.h"

#include "core/file_sys/fssystem/fssystem_compression_configuration.h"
#include "core/file_sys/fssystem/fssystem_crypto_configuration.h"
#include "core/file_sys/fssystem/fssystem_nca_file_system_driver.h"

namespace FileSys {

static u8 MasterKeyIdForKeyGeneration(u8 key_generation) {
    return std::max<u8>(key_generation, 1) - 1;
}

NCA::NCA(VirtualFile file_, const NCA* base_nca, bool allow_missing_base)
    : file(std::move(file_)), keys{Core::Crypto::KeyManager::Instance()} {
    if (file == nullptr) {
        status = Loader::ResultStatus::ErrorNullFile;
        return;
    }

    reader = std::make_shared<NcaReader>();
    if (Result rc = reader->Initialize(file, GetCryptoConfiguration(), GetNcaCompressionConfiguration()); R_FAILED(rc)) {
        // Without the header key every encrypted header reads as garbage, so a bad header
        // usually means the keys were never installed.
        status = keys.HasKey(Core::Crypto::S256KeyType::Header)
                     ? Loader::ResultStatus::ErrorBadNCAHeader
                     : Loader::ResultStatus::ErrorMissingHeaderKey;
        return;
    }

    // A decrypted NCA needs no keys.
    const bool decrypted = reader->GetMagic() == NcaHeader::MagicDecrypted;

    // Ensure we have the proper key area keys to continue.
    const u8 master_key_id = MasterKeyIdForKeyGeneration(reader->GetKeyGeneration());
    if (!decrypted && !keys.HasKey(Core::Crypto::S128KeyType::KeyArea, master_key_id, reader->GetKeyIndex())) {
        LOG_ERROR(Loader, "Cannot decrypt {}: keys for gen 0x{:02X}+ required.", file->GetName(), reader->GetKeyGeneration());
        status = Loader::ResultStatus::ErrorMissingKeyAreaKey;
        return;
    }

    RightsId rights_id{};
    reader->GetRightsId(rights_id.data(), rights_id.size());
    if (!decrypted && rights_id != RightsId{}) {
        // External decryption key required; provide it here.
        u128 rights_id_u128;
        std::memcpy(rights_id_u128.data(), rights_id.data(), sizeof(rights_id));

        auto titlekey =
            keys.GetKey(Core::Crypto::S128KeyType::Titlekey, rights_id_u128[1], rights_id_u128[0]);
        if (titlekey == Core::Crypto::Key128{}) {
            LOG_ERROR(Loader, "Cannot decrypt {}: no title key for rights id {}", file->GetName(),
                      Common::HexToString(rights_id));
            status = Loader::ResultStatus::ErrorMissingTitlekey;
            return;
        }

        if (!keys.HasKey(Core::Crypto::S128KeyType::Titlekek, master_key_id)) {
            LOG_ERROR(Loader, "Cannot decrypt {}: titlekek_{:02x} is missing from prod.keys",
                      file->GetName(), master_key_id);
            status = Loader::ResultStatus::ErrorMissingTitlekek;
            return;
        }

        auto titlekek = keys.GetKey(Core::Crypto::S128KeyType::Titlekek, master_key_id);
        Core::Crypto::AESCipher<Core::Crypto::Key128> cipher(titlekek, Core::Crypto::Mode::ECB);
        cipher.Transcode(titlekey.data(), titlekey.size(), titlekey.data(),
                         Core::Crypto::Op::Decrypt);

        reader->SetExternalDecryptionKey(titlekey.data(), titlekey.size());
    }

    const s32 fs_count = reader->GetFsCount();
    NcaFileSystemDriver fs(base_nca ? base_nca->reader : nullptr, reader);
    for (s32 i = 0; i < fs_count; i++) {
        VirtualFile storage;
        NcaFsHeaderReader header_reader;
        if (Result rc = fs.OpenStorage(&storage, &header_reader, i); R_FAILED(rc)) {
            status = Loader::ResultStatus::ErrorBadNCAHeader;
            return;
        }

        if (header_reader.GetFsType() == NcaFsHeader::FsType::RomFs) {
            files.push_back(std::move(storage));
            romfs = files.back();
        } else if (header_reader.GetFsType() == NcaFsHeader::FsType::PartitionFs) {
            partition_storages.push_back(std::move(storage));
        }

        // Decrypted updates keep their patch tables with encryption type None.
        const auto& patch_info = header_reader.GetPatchInfo();
        if (header_reader.GetEncryptionType() == NcaFsHeader::EncryptionType::AesCtrEx ||
            (decrypted && patch_info.HasIndirectTable() && patch_info.HasAesCtrExTable())) {
            is_update = true;
        }
    }

    if (is_update && base_nca == nullptr && !allow_missing_base) {
        status = Loader::ResultStatus::ErrorMissingBKTRBaseRomFS;
    } else {
        status = Loader::ResultStatus::Success;
    }
}

NCA::~NCA() = default;

Loader::ResultStatus NCA::GetStatus() const {
    return status;
}

void NCA::OpenPartitions() const {
    // Storage validation is eager. Metadata-only callers can skip parsing the partition payloads.
    std::call_once(partitions_opened, [this] {
        std::vector<VirtualDir> opened;
        VirtualDir program;
        VirtualDir startup_logo;
        for (const auto& storage : partition_storages) {
            auto partition = std::make_shared<PartitionFilesystem>(storage);
            if (partition->GetStatus() != Loader::ResultStatus::Success) {
                continue;
            }
            if (IsDirectoryExeFS(partition)) {
                program = partition;
            } else if (IsDirectoryLogoPartition(partition)) {
                startup_logo = partition;
            }
            opened.push_back(std::move(partition));
        }
        dirs = std::move(opened);
        exefs = std::move(program);
        logo = std::move(startup_logo);
    });
}

std::vector<VirtualFile> NCA::GetFiles() const {
    if (status != Loader::ResultStatus::Success) {
        return {};
    }
    return files;
}

std::vector<VirtualDir> NCA::GetSubdirectories() const {
    if (status != Loader::ResultStatus::Success) {
        return {};
    }
    OpenPartitions();
    return dirs;
}

std::string NCA::GetName() const {
    return file->GetName();
}

VirtualDir NCA::GetParentDirectory() const {
    return file->GetContainingDirectory();
}

NCAContentType NCA::GetType() const {
    return static_cast<NCAContentType>(reader->GetContentType());
}

u64 NCA::GetTitleId() const {
    if (is_update) {
        return reader->GetProgramId() | 0x800;
    }
    return reader->GetProgramId();
}

RightsId NCA::GetRightsId() const {
    RightsId result;
    reader->GetRightsId(result.data(), result.size());
    return result;
}

u32 NCA::GetSDKVersion() const {
    return reader->GetSdkAddonVersion();
}

u8 NCA::GetKeyGeneration() const {
    return reader->GetKeyGeneration();
}

bool NCA::IsUpdate() const {
    return is_update;
}

VirtualFile NCA::GetRomFS() const {
    return romfs;
}

VirtualDir NCA::GetExeFS() const {
    OpenPartitions();
    return exefs;
}

VirtualFile NCA::GetBaseFile() const {
    return file;
}

VirtualDir NCA::GetLogoPartition() const {
    OpenPartitions();
    return logo;
}

} // namespace FileSys
