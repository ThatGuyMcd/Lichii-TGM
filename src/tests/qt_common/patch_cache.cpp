// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "qt_common/game_list/patch_cache.h"

TEST_CASE("Patch text refreshes with the content scan", "[qt_common][patch_cache]") {
    GameListPatchCache cache;
    // Independent content states, with two synthetic titles sharing one worker.
    std::array<QString, 2> content{QString{}, QStringLiteral("DLC (1)")};
    std::array<unsigned, 2> reads{};
    const auto read = [&](unsigned title) {
        return cache.Get(100 + title, true, [&] {
            ++reads[title];
            return content[title];
        });
    };
    CHECK(read(0).isEmpty());
    content[0] = QStringLiteral("Update (NAND) (1.0.0)");
    CHECK(read(0) == content[0]);
    CHECK(reads[0] == 2);
    CHECK(read(1) == content[1]);
    CHECK(read(0) == content[0]);
    CHECK(reads[0] == 2);
    CHECK(reads[1] == 1);

    // Another directory fills the manual provider, or a reload changes installed content.
    content = {QStringLiteral("Update (SDMC) (2.0.0)"), QStringLiteral("DLC (1, 2)")};
    cache.Clear();
    CHECK(read(0) == content[0]);
    CHECK(read(1) == content[1]);
    CHECK(reads[0] == 3);
    CHECK(reads[1] == 2);
    content[1] = QStringLiteral("[D] DLC (2)");
    cache.Clear();
    CHECK(read(1) == content[1]);
    content[1].clear();
    cache.Clear();
    CHECK(read(1).isEmpty());
    CHECK(read(1).isEmpty());
    CHECK(reads[1] == 5);

    GameListPatchCache next_worker;
    CHECK(next_worker.Get(100, true, [] { return QStringLiteral("Fresh worker"); }) ==
          QStringLiteral("Fresh worker"));
}

TEST_CASE("Uncached patch text never consumes or replaces cached entries",
          "[qt_common][patch_cache]") {
    GameListPatchCache cache;
    unsigned calls{};
    QString content = QStringLiteral("Cached");
    const auto generate = [&] {
        ++calls;
        return content;
    };
    CHECK(cache.Get(100, true, generate) == QStringLiteral("Cached"));
    content = QStringLiteral("Disabled cache");
    CHECK(cache.Get(100, false, generate) == content);
    content = QStringLiteral("Second uncached read");
    CHECK(cache.Get(100, false, generate) == content);
    CHECK(cache.Get(100, true, generate) == QStringLiteral("Cached"));
    CHECK(calls == 3);
    CHECK(cache.Get(0, true, generate) == content);
    content.clear();
    CHECK(cache.Get(0, true, generate).isEmpty());
    CHECK(calls == 5);
    cache.Clear();
    CHECK(cache.Get(100, true, generate).isEmpty());
    CHECK(calls == 6);
}
