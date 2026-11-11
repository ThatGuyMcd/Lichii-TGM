// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <span>

namespace FrontendCommon {

std::filesystem::path ProfileImportStateDirectory();
bool IsLegacyProfileSource(const std::filesystem::path& source,
                           const std::filesystem::path& destination_root);
bool NeedsLegacyProfileImport();
// Explicit filesystem context for callers that must not inspect the current user's profile.
bool NeedsLegacyProfileImport(const std::filesystem::path& destination_root,
                              std::span<const std::filesystem::path> legacy_sources,
                              const std::filesystem::path& state_directory,
                              bool portable = false,
                              const std::filesystem::path& current_config_directory = {});
void CheckLegacyProfileBeforeConfig();
std::filesystem::path RemapImportedPath(const std::filesystem::path& value,
                                        const std::filesystem::path& source,
                                        const std::filesystem::path& destination);
void RemapImportedConfig(const std::filesystem::path& config_directory,
                         const std::filesystem::path& source,
                         const std::filesystem::path& destination);

} // namespace FrontendCommon
