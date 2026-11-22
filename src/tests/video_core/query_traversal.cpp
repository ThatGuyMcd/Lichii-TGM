// SPDX-FileCopyrightText: Copyright 2026 Lichii-TGM contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include "common/container/unordered_map.h"

using u32 = std::uint32_t;
using u64 = std::uint64_t;
using VAddr = u64;
using QueryLocation = u32;
namespace Core {
constexpr u64 DEVICE_PAGEBITS = 12;
constexpr u64 DEVICE_PAGESIZE = 1ULL << DEVICE_PAGEBITS;
constexpr u64 DEVICE_PAGEMASK = DEVICE_PAGESIZE - 1;
}

struct Cache {
    Common::unordered_map<u64, Common::unordered_map<u32, QueryLocation>> cached_queries;
    std::mutex cache_mutex;
    template <bool remove_from_cache, typename Func>
    void IterateCache(VAddr addr, std::size_t size, Func&& func) {
#include "query_traversal.inc"
    }
    void Add(u32 address) {
        cached_queries[address >> Core::DEVICE_PAGEBITS].insert_or_assign(
            address & Core::DEVICE_PAGEMASK, address);
    }
    std::vector<u32> Oracle(u64 begin, u64 size) const {
        std::vector<u32> result;
        for (u64 page = begin >> 12; page <= (begin + size) >> 12; ++page) {
            const auto found = cached_queries.find(page);
            if (found == cached_queries.end()) continue;
            for (const auto& [offset, location] : found->second) {
                const auto address = page * 4096 + offset;
                if (address < begin + size && address + 4 > begin) result.push_back(location);
            }
        }
        return result;
    }
};

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

// Report payloads and addresses are independent synthetic data. A timestamped
// report writes eight value bytes; a later report can start four bytes into it.
std::array<unsigned char, 8> Payload(const std::vector<u32>& order, u32 base) {
    std::array<unsigned char, 8> bytes{};
    for (u32 address : order) {
        if (address == base) {
            const u64 value = 0x1122334455667788ULL;
            std::memcpy(bytes.data(), &value, 8);
        } else if (address == base + 4) {
            const u32 value = 0xaabbccdd;
            std::memcpy(bytes.data() + 4, &value, 4);
        }
    }
    return bytes;
}

int main() {
    int failures = 0;
    int cases = 0;
    const auto run = [&](const char* name, auto test) {
        ++cases;
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) {
            ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    };
    for (bool dense : {false, true}) {
        run(dense ? "dense ordered payload" : "sparse ordered payload", [=] {
            bool witnessed = false;
            // Do not assume a particular Boost hash-table order. Find a reversed
            // pair in the actual production container and compare against it.
            for (u32 base = 64; base < 2048; base += 16) {
                Cache cache;
                if (dense) for (u32 i = 0; i < 256; ++i) cache.Add(i * 16);
                cache.Add(base); cache.Add(base + 4);
                auto expected = cache.Oracle(base, 8);
                if (expected != std::vector<u32>{base + 4, base}) continue;
                witnessed = true;
                std::vector<u32> actual;
                cache.IterateCache<false>(base, 8, [&](u32 id) { actual.push_back(id); });
                Require(Payload(actual, base) == Payload(expected, base), "overlapping report bytes changed");
                Require(actual == expected, "callback traversal order changed");
                break;
            }
            Require(witnessed, "fixture did not find a reversed pair");
        });
    }
    run("ordered invalidation callbacks", [] {
        bool witnessed = false;
        for (u32 base = 64; base < 2048; base += 16) {
            Cache cache;
            for (u32 i = 0; i < 256; ++i) cache.Add(i * 16);
            cache.Add(base + 4);
            auto expected = cache.Oracle(base, 8);
            if (expected != std::vector<u32>{base + 4, base}) continue;
            witnessed = true;
            std::vector<u32> actual;
            cache.IterateCache<true>(base, 8, [&](u32 id) { actual.push_back(id); });
            Require(actual == expected, "invalidation order changed");
            Require(cache.Oracle(base, 8).empty(), "invalidation failed to remove reports");
            Require(!cache.Oracle(0, 4).empty(), "invalidation removed unrelated report");
            break;
        }
        Require(witnessed, "fixture did not find reversed invalidation pair");
    });
    for (u32 begin : {0U, 31U, 4092U}) {
        for (u32 length : {4U, 32U, 64U}) {
            run("range and removal controls", [=] {
                Cache cache;
                for (u32 i = 0; i < 8192; i += 4) cache.Add(i);
                const auto expected = cache.Oracle(begin, length);
                std::vector<u32> actual;
                cache.IterateCache<false>(begin, length, [&](u32 id) { actual.push_back(id); });
                Require(actual == expected, "read order differs from table traversal");
                actual.clear();
                cache.IterateCache<true>(begin, length, [&](u32 id) { actual.push_back(id); });
                Require(actual == expected, "removal callback order differs");
                Require(cache.Oracle(begin, length).empty(), "matched entries retained");
                Require(!cache.Oracle(8192 - 8, 8).empty(), "unrelated entries removed");
            });
        }
    }
    run("boolean early exit and exhaustive miss", [] {
        Cache cache;
        for (u32 i = 0; i < 1024; i += 4) cache.Add(i);
        unsigned calls = 0;
        cache.IterateCache<false>(128, 8, [&](u32) { ++calls; return true; });
        Require(calls == 1, "existence predicate did not stop");
        std::vector<u32> visited;
        cache.IterateCache<false>(128, 8, [&](u32 id) { visited.push_back(id); return false; });
        auto expected = cache.Oracle(128, 8);
        std::ranges::sort(expected); std::ranges::sort(visited);
        Require(visited == expected, "false predicate lost or duplicated reports");
        calls = 0;
        cache.IterateCache<false>(3000, 4, [&](u32) { ++calls; return true; });
        Require(calls == 0, "missing report invoked predicate");
    });
    run("single matching report", [] {
        Cache cache;
        for (u32 i = 0; i < 1024; i += 16) cache.Add(i);
        std::vector<u32> visited;
        cache.IterateCache<false>(130, 1, [&](u32 id) { visited.push_back(id); });
        Require(visited == std::vector<u32>{128}, "partial overlap did not flush once");
    });
    std::cout << cases << " cases, " << failures << " failures\n";
    return failures ? 1 : 0;
}
