// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string>
#include <functional>
#include <memory>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "common/unique_function.h"

namespace {
struct Noisy {
    Noisy() : state{"Default constructed"} {}
    Noisy(Noisy&& rhs) noexcept : state{"Move constructed"} {
        rhs.state = "Moved away";
    }
    Noisy& operator=(Noisy&& rhs) noexcept {
        state = "Move assigned";
        rhs.state = "Moved away";
        return *this;
    }
    Noisy(const Noisy&) : state{"Copied constructed"} {}
    Noisy& operator=(const Noisy&) {
        state = "Copied assigned";
        return *this;
    }

    std::string state;
};
} // Anonymous namespace

TEST_CASE("UniqueFunction", "[common]") {
    SECTION("Capture reference") {
        int value = 0;
        Common::UniqueFunction<void> func = [&value] { value = 5; };
        func();
        REQUIRE(value == 5);
    }
    SECTION("Capture pointer") {
        int value = 0;
        int* pointer = &value;
        Common::UniqueFunction<void> func = [pointer] { *pointer = 5; };
        func();
        REQUIRE(value == 5);
    }
    SECTION("Move object") {
        Noisy noisy;
        REQUIRE(noisy.state == "Default constructed");

        Common::UniqueFunction<void> func = [noisy_inner = std::move(noisy)] {
            REQUIRE(noisy_inner.state == "Move constructed");
        };
        REQUIRE(noisy.state == "Moved away");
        func();
    }
    SECTION("Move construct function") {
        int value = 0;
        Common::UniqueFunction<void> func = [&value] { value = 5; };
        Common::UniqueFunction<void> new_func = std::move(func);
        new_func();
        REQUIRE(value == 5);
    }
    SECTION("Move assign function") {
        int value = 0;
        Common::UniqueFunction<void> func = [&value] { value = 5; };
        Common::UniqueFunction<void> new_func;
        new_func = std::move(func);
        new_func();
        REQUIRE(value == 5);
    }
    SECTION("Default construct then assign function") {
        int value = 0;
        Common::UniqueFunction<void> func;
        func = [&value] { value = 5; };
        func();
        REQUIRE(value == 5);
    }
    SECTION("Pass arguments") {
        int result = 0;
        Common::UniqueFunction<void, int, int> func = [&result](int a, int b) { result = a + b; };
        func(5, 4);
        REQUIRE(result == 9);
    }
    SECTION("Pass arguments and return value") {
        Common::UniqueFunction<int, int, int> func = [](int a, int b) { return a + b; };
        REQUIRE(func(5, 4) == 9);
    }
    SECTION("Destructor") {
        int num_destroyed = 0;
        struct Foo {
            Foo(int* num_) : num{num_} {}
            Foo(Foo&& rhs) : num{std::exchange(rhs.num, nullptr)} {}
            Foo(const Foo&) = delete;

            ~Foo() {
                if (num) {
                    ++*num;
                }
            }

            int* num = nullptr;
        };
        Foo object{&num_destroyed};
        {
            Common::UniqueFunction<void> func = [object_inner = std::move(object)] {};
            REQUIRE(num_destroyed == 0);
        }
        REQUIRE(num_destroyed == 1);
    }
}

TEST_CASE("UniqueFunction owns a snapshot of a const lvalue callable",
          "[common][unique_function_ownership]") {
    struct Counter {
        mutable int value;
        int operator()() const {
            return ++value;
        }
    };
    const Counter source{11};
    Common::UniqueFunction<int> queued{source};
    source.value = 90;
    CHECK(queued() == 12);
    CHECK(queued() == 13);
    CHECK(source.value == 90);
}

TEST_CASE("UniqueFunction retains its callable after the submitting scope ends",
          "[common][unique_function_ownership]") {
    struct Job {
        int& live;
        int& copies;
        int value;
        Job(int& live_, int& copies_, int value_) : live{live_}, copies{copies_}, value{value_} {
            ++live;
        }
        Job(const Job& other) : live{other.live}, copies{other.copies}, value{other.value} {
            ++live;
            ++copies;
        }
        ~Job() {
            --live;
        }
        int operator()() const {
            return value;
        }
    };
    int live = 0;
    int copies = 0;
    {
        Common::UniqueFunction<int> queued;
        {
            const Job source{live, copies, 37};
            queued = source;
            // Stop before leaving the scope if the wrapper borrowed the callable.
            REQUIRE(live == 2);
            REQUIRE(copies == 1);
        }
        CHECK(live == 1);
        CHECK(queued() == 37);
    }
    CHECK(live == 0);
}

TEST_CASE("UniqueFunction preserves explicit sharing and supports ordinary invocable forms",
          "[common][unique_function_ownership]") {
    struct Counter {
        int value;
        int Next() { return ++value; }
        int operator()() { return Next(); }
    };
    Counter source{5};
    Common::UniqueFunction<int> owned{source};
    Common::UniqueFunction<int> shared{std::ref(source)};
    CHECK(owned() == 6);
    CHECK(source.value == 5);
    CHECK(shared() == 6);
    CHECK(source.value == 6);
    Common::UniqueFunction<int, Counter&> member{&Counter::Next};
    CHECK(member(source) == 7);
    Common::UniqueFunction<int, Counter&> data{&Counter::value};
    CHECK(data(source) == 7);
    Common::UniqueFunction<void> discard{[&source] { return source.Next(); }};
    discard();
    CHECK(source.value == 8);
    Common::UniqueFunction<int> move_only{[value = std::make_unique<int>(71)] { return *value; }};
    auto moved = std::move(move_only);
    CHECK_FALSE(move_only);
    CHECK(moved() == 71);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<Common::UniqueFunction<int>>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<Common::UniqueFunction<int>, int>);
    STATIC_REQUIRE_FALSE(std::is_constructible_v<Common::UniqueFunction<int>,
                          decltype([] {})>);
}
