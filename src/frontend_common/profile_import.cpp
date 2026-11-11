// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#ifndef SI_NO_CONVERSION
#define SI_NO_CONVERSION 1
#endif
#include <SimpleIni.h>
#include "common/fs/path_util.h"
#include "common/fs/profile_migration.h"
#include "frontend_common/profile_import.h"

namespace FrontendCommon {
namespace fs = std::filesystem;
namespace Migration = Common::FS::ProfileMigration;

fs::path ProfileImportStateDirectory() {
    const auto& root = Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir);
    auto name = root.filename();
    name += ".migration";
    return root.parent_path() / name;
}

namespace {
bool SameProfileRoot(const fs::path& left, const fs::path& right) {
    std::error_code error;
    if (fs::equivalent(left, right, error)) {
        return true;
    }
    // Neither root need exist on the first launch. Resolve existing prefixes and compare the
    // remaining native components too, rather than treating the active Eden root as a source.
    auto normalized_left = fs::weakly_canonical(left);
    auto normalized_right = fs::weakly_canonical(right);
    if (normalized_left.filename().empty()) {
        normalized_left = normalized_left.parent_path();
    }
    if (normalized_right.filename().empty()) {
        normalized_right = normalized_right.parent_path();
    }
#ifdef _WIN32
    auto left_text = normalized_left.wstring();
    auto right_text = normalized_right.wstring();
    std::transform(left_text.begin(), left_text.end(), left_text.begin(), ::towlower);
    std::transform(right_text.begin(), right_text.end(), right_text.begin(), ::towlower);
    return left_text == right_text;
#else
    return normalized_left == normalized_right;
#endif
}
} // namespace

bool IsLegacyProfileSource(const fs::path& source, const fs::path& destination_root) {
    return !SameProfileRoot(source, destination_root) && fs::is_directory(source);
}

bool NeedsLegacyProfileImport(const fs::path& destination_root,
                              std::span<const fs::path> legacy_sources,
                              const fs::path& state_directory, bool portable,
                              const fs::path& current_config_directory) {
    if (portable) {
        return false;
    }
    const auto state = Migration::GetState(state_directory);
    if (state == Migration::State::Pending) {
        return true;
    }
    if (state != Migration::State::Unresolved) {
        return false;
    }
    // TGM continues using the Eden profile in place. An existing global configuration belongs to
    // this profile even when other emulators are installed. Directory creation alone is not
    // evidence of a configured profile, and an unfinished transaction above always wins.
    const auto config_directory = current_config_directory.empty()
                                      ? destination_root / "config"
                                      : current_config_directory;
    if (fs::is_regular_file(config_directory / "qt-config.ini") ||
        fs::is_regular_file(config_directory / "sdl2-config.ini")) {
        return false;
    }
    for (const auto& source : legacy_sources) {
        if (IsLegacyProfileSource(source, destination_root)) {
            return true;
        }
    }
    return false;
}

bool NeedsLegacyProfileImport() {
#ifdef __ANDROID__
    return false;
#else
    using namespace Common::FS;
    constexpr std::array sources{LegacyEdenDir, CitronDir, SudachiDir, SuyuDir, YuzuDir};
    std::array<fs::path, sources.size()> paths;
    std::ranges::transform(sources, paths.begin(), [](EmuPath source) {
        return GetLegacyPath(source);
    });
    return NeedsLegacyProfileImport(GetEdenPath(EdenPath::EdenDir), paths,
                                    ProfileImportStateDirectory(), IsPortableMode(),
                                    GetEdenPath(EdenPath::ConfigDir));
#endif
}

void CheckLegacyProfileBeforeConfig() {
    if (NeedsLegacyProfileImport()) {
        throw std::runtime_error("Legacy emulator data or an unfinished import was found. "
                                 "Open the Lichii GUI to import it or explicitly start fresh "
                                 "before using eden-cli. No configuration was created.");
    }
}

fs::path RemapImportedPath(const fs::path& value, const fs::path& source,
                           const fs::path& destination) {
    if (value.empty() || !value.is_absolute()) {
        return value;
    }
    const auto normalized = value.lexically_normal();
    auto root = source.lexically_normal();
    if (root.filename().empty()) {
        root = root.parent_path();
    }
    auto current = normalized.begin();
    for (const auto& component : root) {
        if (current == normalized.end()) {
            return value;
        }
#ifdef _WIN32
        auto left = component.wstring();
        auto right = current->wstring();
        std::transform(left.begin(), left.end(), left.begin(), ::towlower);
        std::transform(right.begin(), right.end(), right.begin(), ::towlower);
        if (left != right) {
            return value;
        }
#else
        if (component != *current) {
            return value;
        }
#endif
        ++current;
    }
    auto mapped = destination;
    for (; current != normalized.end(); ++current) {
        mapped /= *current;
    }
    return mapped.lexically_normal();
}

void RemapImportedConfig(const fs::path& config_directory, const fs::path& source,
                         const fs::path& destination) {
    if (!fs::is_directory(config_directory)) {
        return;
    }
    constexpr std::array storage_keys{"nand_directory", "save_directory", "sdmc_directory",
                                      "load_directory", "tas_directory",  "dump_directory"};
    for (const auto& entry : fs::recursive_directory_iterator(config_directory)) {
        if (entry.path().extension() != ".ini" || !entry.is_regular_file()) {
            continue;
        }
        std::ifstream input{entry.path(), std::ios::binary};
        const std::string data{std::istreambuf_iterator<char>{input}, {}};
        if (!input || input.bad()) {
            throw std::runtime_error("Could not read imported configuration");
        }
        CSimpleIniA ini;
        ini.SetUnicode();
        if (ini.LoadData(data) < 0) {
            throw std::runtime_error("Could not parse imported configuration");
        }
        bool changed = false;
        const auto remap = [&](const char* section, const char* key) {
            const auto* value = ini.GetValue(section, key);
            if (!value) {
                return;
            }
            std::string decoded{value};
            std::erase(decoded, '"');
            const auto path = Common::FS::ToU8String(decoded);
            const auto mapped = RemapImportedPath(fs::path{path}, source, destination);
            if (mapped != fs::path{path}) {
                const auto text = Common::FS::PathToUTF8String(mapped);
                if (ini.SetValue(section, key, text.c_str()) < 0) {
                    throw std::runtime_error("Could not remap imported configuration");
                }
                changed = true;
            }
        };
        for (const auto* key : storage_keys) {
            remap("Data%20Storage", key);
        }
        remap("Screenshots", "screenshot_path");
        if (changed) {
            std::string output;
            if (ini.Save(output) < 0) {
                throw std::runtime_error("Could not serialize imported configuration");
            }
            std::ofstream file{entry.path(), std::ios::binary | std::ios::trunc};
            file << output;
            file.close();
            if (!file) {
                throw std::runtime_error("Could not write imported configuration");
            }
        }
    }
}

} // namespace FrontendCommon
