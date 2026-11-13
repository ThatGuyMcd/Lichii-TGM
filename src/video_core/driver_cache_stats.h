// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <chrono>
#include <filesystem>
#include <optional>

namespace VideoCommon {
struct DriverCacheStats {
    unsigned files{};
    std::uintmax_t bytes{};
    std::optional<std::filesystem::file_time_type> newest;
    bool incomplete{};
};

inline DriverCacheStats ReadDriverCacheStats(const std::filesystem::path& folder,
                                            unsigned limit = 4096) {
    DriverCacheStats result;
    std::error_code error;
    std::filesystem::recursive_directory_iterator it{
        folder, std::filesystem::directory_options::skip_permission_denied, error}, end;
    unsigned visited{};
    for (; !error && it != end && visited < limit; it.increment(error), ++visited) {
        std::error_code entry_error;
        if (!it->is_regular_file(entry_error)) continue;
        ++result.files;
        const auto size = it->file_size(entry_error);
        if (!entry_error) result.bytes += size;
        else result.incomplete = true;
        entry_error.clear();
        const auto time = it->last_write_time(entry_error);
        if (!entry_error) {
            if (!result.newest || time > *result.newest) result.newest = time;
        } else {
            result.incomplete = true;
        }
    }
    result.incomplete |= bool(error) || it != end;
    return result;
}

inline std::optional<long long> DriverCacheAgeSeconds(
    const DriverCacheStats& stats,
    std::filesystem::file_time_type now = std::filesystem::file_time_type::clock::now()) {
    if (!stats.newest) return std::nullopt;
    if (*stats.newest >= now) return 0;
    return std::chrono::duration_cast<std::chrono::seconds>(now - *stats.newest).count();
}
} // namespace VideoCommon
