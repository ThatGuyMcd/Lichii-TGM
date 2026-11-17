// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/nx2_syncpoint_poll.h"

TEST_CASE("Rearmed syncpoint waits observe advances already written into the guest mirror",
          "[nx2_syncpoint_poll]") {
    Service::Nx2SyncpointPollSnapshot<192> snapshot;
    std::array<u64, 3> changed{};
    std::array<u64, 3> armed{1, 1, u64{1} << 63};
    std::array<u32, 192> guest_mirror{};
    snapshot.Observe(0, 7);
    snapshot.Observe(64, 0xffffffff);
    snapshot.Observe(191, 2);
    guest_mirror[0] = 8; // Immediate callback publishes this before PollSyncpts.
    guest_mirror[64] = 0; // Wraparound is an advance too.
    guest_mirror[191] = 3;
    snapshot.Observe(0, guest_mirror[0], changed);
    snapshot.Observe(64, guest_mirror[64], changed);
    snapshot.Observe(191, guest_mirror[191], changed);
    CHECK(changed == armed);
    for (size_t word = 0; word < armed.size(); ++word) {
        CHECK((armed[word] & changed[word]) != 0);
    }
    changed.fill(0);
    snapshot.Observe(0, 8, changed);
    snapshot.Observe(64, 0, changed);
    snapshot.Observe(191, 3, changed);
    CHECK(changed == std::array<u64, 3>{});
    snapshot.Observe(63, 1, changed); // Another, unarmed wait must not notify these waits.
    snapshot.Observe(65, 1, changed);
    snapshot.Observe(192, 1, changed); // Outside the host syncpoint array.
    CHECK(changed[0] == (u64{1} << 63));
    CHECK(changed[1] == 2);
    CHECK(changed[2] == 0);
    for (size_t word = 0; word < armed.size(); ++word) {
        CHECK((armed[word] & changed[word]) == 0);
    }
}
