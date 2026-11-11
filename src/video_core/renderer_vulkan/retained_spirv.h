// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstring>
#include <optional>
#include <span>
#include <vector>
#include "common/common_types.h"
#include "common/zstd_compression.h"

namespace Vulkan {

inline constexpr size_t MaxRetainedSpirvBytes = 64 * 1024 * 1024;

inline std::optional<std::vector<u32>> DecodeRetainedSpirv(std::span<const u8> compressed,
                                                         size_t max_bytes) {
    const auto bytes = Common::Compression::TryDecompressDataZSTD(compressed, max_bytes);
    if (!bytes || bytes->size() < 5 * sizeof(u32) || bytes->size() % sizeof(u32) != 0) {
        return std::nullopt;
    }
    std::vector<u32> words(bytes->size() / sizeof(u32));
    std::memcpy(words.data(), bytes->data(), bytes->size());
    if (words[0] != 0x07230203 || words[4] != 0) {
        return std::nullopt;
    }
    return words;
}

} // namespace Vulkan
