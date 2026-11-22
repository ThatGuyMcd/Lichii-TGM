// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "common/common_types.h"
#include "core/loader/loader.h"

namespace Core {
class System;
}

namespace FileSys {
class NACP;
}

namespace Kernel {
class KProcess;
}

namespace Loader {

struct NroLayout {
    struct Segment {
        u64 offset{};
        u64 size{};
    };
    // .text, .rodata and .data, with .data extended to cover .bss.
    std::array<Segment, 3> segments{};
    u64 file_size{};
    u64 image_size{};
    std::string_view ignored_module_header{};
};

/// Checks the program image using only bounded header reads. Assets are outside its extent.
std::optional<NroLayout> ValidateNroLayout(const FileSys::VfsFile& file,
                                          std::string_view* reason = nullptr);

/// Loads an NRO file
class AppLoader_NRO final : public AppLoader {
public:
    explicit AppLoader_NRO(FileSys::VirtualFile file_);
    ~AppLoader_NRO() override;

    /**
     * Identifies whether or not the given file is an NRO file.
     *
     * @param nro_file The file to identify.
     *
     * @return FileType::NRO, or FileType::Error if the file is not an NRO file.
     */
    static FileType IdentifyType(const FileSys::VirtualFile& nro_file);

    bool IsHomebrew();

    FileType GetFileType() const override {
        return IdentifyType(file);
    }

    LoadResult Load(Kernel::KProcess& process, Core::System& system) override;

    ResultStatus ReadIcon(std::vector<u8>& buffer) override;
    ResultStatus ReadProgramId(u64& out_program_id) override;
    ResultStatus ReadRomFS(FileSys::VirtualFile& dir) override;
    ResultStatus ReadTitle(std::string& title) override;
    ResultStatus ReadControlData(FileSys::NACP& control) override;
    bool IsRomFSUpdatable() const override;

private:
    bool LoadNro(Core::System& system, Kernel::KProcess& process, const FileSys::VfsFile& nro_file);

    std::vector<u8> icon_data;
    std::unique_ptr<FileSys::NACP> nacp;
    FileSys::VirtualFile romfs;
};

} // namespace Loader
