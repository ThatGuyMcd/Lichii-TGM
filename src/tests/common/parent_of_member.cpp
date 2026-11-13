// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <thread>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/common_types.h"
#include "common/parent_of_member.h"

namespace {
struct Prefix {
    virtual ~Prefix() = default;
    u8 prefix{};
};
struct Suffix {
    u64 suffix[7]{};
};
struct Parent : Prefix, Suffix {
    alignas(64) u32 member{};
    u64 other{};
};
struct ColdParent : Prefix {
    u8 padding[513]{};
    alignas(32) u64 member{};
};
}

TEST_CASE("Parent lookup preserves derived, aligned and const object identities", "[common][parent_of_member]") {
    Parent objects[3];
    for (auto& object : objects) {
        CHECK(Common::GetParentPointer<&Parent::member>(&object.member) == &object);
        CHECK(Common::GetParentPointer<&Parent::member>(object.member) == &object);
        CHECK(&Common::GetParentReference<&Parent::member>(object.member) == &object);
        CHECK(Common::GetParentPointer<&Parent::other>(&object.other) == &object);
        const auto& immutable = object;
        CHECK(Common::GetParentPointer<&Parent::member>(&immutable.member) == &immutable);
        CHECK(&Common::GetParentReference<&Parent::member>(immutable.member) == &immutable);
        CHECK((Common::GetParentPointer<&Prefix::prefix, Parent>(&object.prefix) == &object));
    }
}

TEST_CASE("Parent offset initialization is shared safely by concurrent first callers", "[common][parent_of_member]") {
    std::atomic<bool> correct{true};
    std::vector<std::jthread> workers;
    for (u32 thread = 0; thread < 8; ++thread) {
        workers.emplace_back([&] {
            ColdParent object;
            for (u32 call = 0; call < 10000; ++call) {
                if (Common::GetParentPointer<&ColdParent::member>(&object.member) != &object) {
                    correct.store(false, std::memory_order_relaxed);
                }
            }
        });
    }
    workers.clear();
    CHECK(correct.load(std::memory_order_relaxed));
}
