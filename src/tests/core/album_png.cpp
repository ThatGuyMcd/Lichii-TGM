// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/fs/path_util.h"
#include "common/scope_exit.h"
#include "common/stb.h"
#include "core/core.h"
#include "core/hle/service/caps/caps_manager.h"
#include "tests/core/system_test_context.h"

TEST_CASE("Album reads a generated PNG at full and thumbnail sizes", "[.][album_png]") {
    using namespace Service::Capture;
    namespace FS = Common::FS;
    ScopedSystemTestContext context;
    const auto screenshots = FS::GetEdenPath(FS::EdenPath::NANDDir) / "synthetic-album";
    REQUIRE(std::filesystem::create_directory(screenshots));
    const auto previous_path = FS::GetEdenPath(FS::EdenPath::ScreenshotsDir);
    FS::SetEdenPath(FS::EdenPath::ScreenshotsDir, screenshots);
    SCOPE_EXIT {
        FS::SetEdenPath(FS::EdenPath::ScreenshotsDir, previous_path);
    };

    constexpr std::array<u8, 4> pixel{19, 74, 183, 255};
    constexpr std::array pixels{pixel, pixel, pixel, pixel};
    {
        std::ofstream file{screenshots / "000000000000002a_2026-01-02_03-04-05-000.png",
                           std::ios::binary};
        REQUIRE(file);
        const auto writer = [](void* output_file, void* data, int length) {
            static_cast<std::ofstream*>(output_file)->write(static_cast<const char*>(data), length);
        };
        REQUIRE(stbi_write_png_to_func(writer, &file, 2, 2, STBI_rgb_alpha, pixels.data(), 0));
        file.close();
        REQUIRE(file);
    }

    Core::System system;
    AlbumManager album{system};
    REQUIRE(album.IsAlbumMounted(AlbumStorage::Sd).IsSuccess());
    std::array<AlbumEntry, 1> entries{};
    u64 count{};
    REQUIRE(album.GetAlbumFileList(entries, count, AlbumStorage::Sd, 0).IsSuccess());
    REQUIRE(count == entries.size());
    REQUIRE(entries[0].file_id.application_id == 0x2a);

    for (const bool thumbnail : {false, true}) {
        CAPTURE(thumbnail);
        const auto width = thumbnail ? 320 : 1280;
        const auto height = thumbnail ? 180 : 720;
        std::vector<std::array<u8, 4>> decoded(width * height);
        std::span<u8> output{reinterpret_cast<u8*>(decoded.data()), decoded.size() * sizeof(pixel)};
        LoadAlbumScreenShotImageOutput info{};
        const auto result = thumbnail
            ? album.LoadAlbumScreenShotThumbnail(info, output, entries[0].file_id, {})
            : album.LoadAlbumScreenShotImage(info, output, entries[0].file_id, {});
        REQUIRE(result.IsSuccess());
        CHECK(info.width == width);
        CHECK(info.height == height);
        CHECK(std::ranges::all_of(decoded, [&](const auto& value) { return value == pixel; }));
    }
}
