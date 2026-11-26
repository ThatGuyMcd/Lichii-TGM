// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/system_archive/system_archive.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "core/hle/service/ns/platform_service_manager.h"

namespace {

FileSys::VirtualFile FontArchive(std::string_view name, std::vector<u8> contents) {
    std::vector<FileSys::VirtualFile> files{
        std::make_shared<FileSys::VectorVfsFile>(std::move(contents), std::string(name))};
    return FileSys::CreateRomFS(std::make_shared<FileSys::VectorVfsDirectory>(std::move(files)));
}

TEST_CASE("Shared fonts use a built-in archive when the installed entry cannot be read",
          "[core][shared_font_fallback]") {
    constexpr std::string_view filename = "independent-font.bfttf";
    const auto make_font = [](u32 word) {
        std::vector<u8> bytes(3 * sizeof(u32));
        std::size_t offset{};
        Service::NS::EncryptSharedFont({word}, bytes, offset);
        return bytes;
    };
    const auto installed = make_font(0x01020304);
    const auto builtin = make_font(0x18273645);
    const auto fallback_archive = FontArchive(filename, builtin);
    const auto installed_archive = FontArchive(filename, installed);
    REQUIRE(fallback_archive);
    REQUIRE(installed_archive);

    int fallback_calls = 0;
    const auto fallback = [&] {
        ++fallback_calls;
        return fallback_archive;
    };
    constexpr std::size_t capacity = 256;
    const auto expected_builtin =
        Service::NS::ReadSharedFont(fallback_archive, filename, capacity, fallback);
    REQUIRE(expected_builtin.size() == 3);
    const auto current = Service::NS::ReadSharedFont(installed_archive, filename, capacity, fallback);
    REQUIRE(current.size() == 3);
    CHECK(current != expected_builtin);
    CHECK(fallback_calls == 0);
    for (const auto& invalid : {FileSys::VirtualFile{}, FontArchive("other-name", installed),
                                FontArchive(filename, std::vector<u8>(12)),
                                FontArchive(filename, std::vector<u8>(11))}) {
        CHECK(Service::NS::ReadSharedFont(invalid, filename, capacity, fallback) ==
              expected_builtin);
    }
    CHECK(fallback_calls == 4);
    CHECK(Service::NS::ReadSharedFont(installed_archive, filename, 12, fallback).empty());
    CHECK(fallback_calls == 5);
}

u16 Be16(const std::vector<u8>& data, size_t offset) {
    REQUIRE(offset + 2 <= data.size());
    return static_cast<u16>((data[offset] << 8) | data[offset + 1]);
}

u32 Be32(const std::vector<u8>& data, size_t offset) {
    return (u32{Be16(data, offset)} << 16) | Be16(data, offset + 2);
}

// The standard font as the service decrypts it when there is no firmware font.
std::vector<u8> SynthesizedStandardFont() {
    const auto archive = FileSys::SystemArchive::SynthesizeSystemArchive(
        static_cast<u64>(Service::NS::FontArchives::Standard));
    REQUIRE(archive != nullptr);
    const auto romfs = FileSys::ExtractRomFS(archive);
    REQUIRE(romfs != nullptr);
    const auto file = romfs->GetFile(Service::NS::SHARED_FONTS[0].second);
    REQUIRE(file != nullptr);
    std::vector<u32> words(file->GetSize() / sizeof(u32));
    file->ReadBytes<u32>(words.data(), file->GetSize());
    std::transform(words.begin(), words.end(), words.begin(), Common::swap32);
    std::vector<u8> font(file->GetSize() - 8);
    Service::NS::DecryptSharedFontToTTF(words, font);
    return font;
}

size_t FindTable(const std::vector<u8>& font, std::string_view tag) {
    const u16 count = Be16(font, 4);
    for (u16 i = 0; i < count; ++i) {
        const size_t record = 12 + size_t{i} * 16;
        REQUIRE(record + 16 <= font.size());
        if (std::string_view{reinterpret_cast<const char*>(&font[record]), 4} == tag) {
            return Be32(font, record + 8);
        }
    }
    FAIL("no " << tag << " table");
    return 0;
}

// The glyph index of a code point in the Windows Unicode BMP (format 4) character map.
std::optional<u16> GlyphIndex(const std::vector<u8>& font, u16 code_point) {
    const size_t cmap = FindTable(font, "cmap");
    const u16 subtables = Be16(font, cmap + 2);
    for (u16 i = 0; i < subtables; ++i) {
        const size_t record = cmap + 4 + size_t{i} * 8;
        const size_t table = cmap + Be32(font, record + 4);
        if (Be16(font, record) != 3 || Be16(font, record + 2) != 1 || Be16(font, table) != 4) {
            continue;
        }
        const size_t segments = Be16(font, table + 6) / 2;
        const size_t ends = table + 14;
        const size_t starts = ends + segments * 2 + 2;
        const size_t deltas = starts + segments * 2;
        const size_t range_offsets = deltas + segments * 2;
        for (size_t s = 0; s < segments; ++s) {
            if (code_point > Be16(font, ends + s * 2)) {
                continue;
            }
            const u16 start = Be16(font, starts + s * 2);
            if (code_point < start) {
                return std::nullopt;
            }
            const u16 delta = Be16(font, deltas + s * 2);
            const size_t range_offset_at = range_offsets + s * 2;
            const u16 range_offset = Be16(font, range_offset_at);
            if (range_offset == 0) {
                return static_cast<u16>(code_point + delta);
            }
            const u16 glyph =
                Be16(font, range_offset_at + range_offset + static_cast<size_t>(code_point - start) * 2);
            return glyph == 0 ? std::nullopt : std::optional<u16>{static_cast<u16>(glyph + delta)};
        }
        return std::nullopt;
    }
    FAIL("no Windows Unicode BMP character map");
    return std::nullopt;
}

// The number of contours of a glyph's outline, 0 when it has none.
int ContourCount(const std::vector<u8>& font, u16 glyph) {
    const bool long_offsets = Be16(font, FindTable(font, "head") + 50) != 0;
    const size_t loca = FindTable(font, "loca");
    const auto offset = [&](u32 index) -> size_t {
        return long_offsets ? Be32(font, loca + size_t{index} * 4)
                            : size_t{Be16(font, loca + size_t{index} * 2)} * 2;
    };
    if (offset(glyph + 1) == offset(glyph)) {
        return 0;
    }
    return static_cast<s16>(Be16(font, FindTable(font, "glyf") + offset(glyph)));
}

} // Anonymous namespace

TEST_CASE("Synthesized standard font draws the controller button icons", "[font]") {
    // Switch 2 titles look these up in the standard font. Mario Tennis Fever draws U+E0AD (the
    // top face button) before "Details"; without a glyph it shows "?".
    constexpr std::array<u16, 29> button_icons{
        0xE0A0, 0xE0A1, 0xE0A2, 0xE0A3, 0xE0A4, 0xE0A5, 0xE0A6, 0xE0A7, 0xE0A8, 0xE0A9,
        0xE0AA, 0xE0AB, 0xE0AC, 0xE0AD, 0xE0AE, 0xE0B3, 0xE0B4, 0xE0E0, 0xE0E1, 0xE0E2,
        0xE0E3, 0xE0E4, 0xE0E5, 0xE0E6, 0xE0E7, 0xE0E8, 0xE0E9, 0xE0EF, 0xE0F0,
    };
    const std::vector<u8> font = SynthesizedStandardFont();
    REQUIRE(Be32(font, 0) == 0x00010000);
    for (const u16 code_point : button_icons) {
        INFO("U+" << std::hex << code_point);
        const auto glyph = GlyphIndex(font, code_point);
        REQUIRE(glyph.has_value());
        CHECK(ContourCount(font, *glyph) > 0);
    }
    CHECK_FALSE(GlyphIndex(font, 0xE0AF).has_value());
}
