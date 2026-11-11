// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include "video_core/renderer_vulkan/retained_spirv.h"

TEST_CASE("Retained graphics code preserves exact module words and rejects damaged inputs",
          "[video_core][retained_spirv]") {
    const std::array<u32, 6> words{0x07230203, 0x00010300, 0x12345678, 3, 0, 0x00010000};
    const auto pack = [](std::span<const u32> code) {
        return Common::Compression::CompressDataZSTD(
            reinterpret_cast<const u8*>(code.data()), code.size_bytes(), 1);
    };
    const auto compressed = pack(words);
    const auto restored = Vulkan::DecodeRetainedSpirv(compressed, sizeof(words));
    REQUIRE(restored.has_value());
    CHECK(std::ranges::equal(*restored, words));
    CHECK_FALSE(Vulkan::DecodeRetainedSpirv(compressed, sizeof(words) - 1));
    auto truncated = compressed;
    truncated.pop_back();
    CHECK_FALSE(Vulkan::DecodeRetainedSpirv(truncated, sizeof(words)));
    auto bad = words;
    bad[0] = 0;
    CHECK_FALSE(Vulkan::DecodeRetainedSpirv(pack(bad), sizeof(bad)));
    bad = words;
    bad[4] = 1;
    CHECK_FALSE(Vulkan::DecodeRetainedSpirv(pack(bad), sizeof(bad)));
    CHECK_FALSE(Vulkan::DecodeRetainedSpirv({}, sizeof(words)));
}
