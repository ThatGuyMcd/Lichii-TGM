// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <charconv>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace Vulkan::Ngx {
inline std::optional<std::vector<unsigned>> RuntimeVersion(std::string_view name) {
    constexpr std::string_view prefix = "libnvidia-ngx-dlss.so.";
    if (!name.starts_with(prefix)) {
        return std::nullopt;
    }
    name.remove_prefix(prefix.size());
    std::vector<unsigned> parts;
    while (!name.empty()) {
        const auto end = name.find('.');
        const auto word = name.substr(0, end);
        unsigned value{};
        const auto parsed = std::from_chars(word.data(), word.data() + word.size(), value);
        if (word.empty() || parsed.ec != std::errc{} || parsed.ptr != word.data() + word.size()) {
            return std::nullopt;
        }
        parts.push_back(value);
        if (end == std::string_view::npos) {
            return parts;
        }
        name.remove_prefix(end + 1);
    }
    return std::nullopt;
}

inline std::filesystem::path FindRuntimeInDirectory(const std::filesystem::path& directory) {
    for (const char* name : {"libnvidia-ngx-dlss.so", "libnvngx_dlss.so"}) {
        std::error_code error;
        const auto file = directory / name;
        if (std::filesystem::is_regular_file(file, error)) {
            return file;
        }
    }
    std::error_code error;
    std::filesystem::path best;
    std::vector<unsigned> best_version;
    for (std::filesystem::directory_iterator it{
             directory, std::filesystem::directory_options::skip_permission_denied, error}, end;
         !error && it != end; it.increment(error)) {
        const auto version = RuntimeVersion(it->path().filename().string());
        std::error_code entry_error;
        if (version && *version > best_version && it->is_regular_file(entry_error)) {
            best = it->path();
            best_version = *version;
        }
    }
    return best;
}
} // namespace Vulkan::Ngx
