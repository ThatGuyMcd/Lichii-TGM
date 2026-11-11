// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/upload_batch.h"
#include "video_core/renderer_vulkan/pending_submission.h"

TEST_CASE("An upload-only submission stays pending through batching and command recording",
          "[video_core][upload_batch][submission_policy]") {
    Vulkan::PendingSubmission pending;
    pending.Record();
    pending.Submitted();
    REQUIRE_FALSE(pending.HasWork());

    const auto source = reinterpret_cast<VkBuffer>(uintptr_t{1});
    const auto destination = reinterpret_cast<VkBuffer>(uintptr_t{2});
    const std::array<VkBufferCopy, 1> copies{{{4, 8, 4}}};
    SECTION("Reordered copies alone require a submission") {
        Vulkan::ReorderedUploadBatch batch;
        batch.Add(source, destination, copies, 17);
        pending.QueueReorderedUploads();
        // No command has been recorded since the previous submission. These copies are still
        // work: skipping this submission would leave them queued and its fence unfinished.
        REQUIRE(pending.HasWork());
        REQUIRE_FALSE(pending.UploadsPending());
        std::vector<VkBufferCopy> drained;
        std::vector<Vulkan::UploadBatch::Group> groups;
        u64 prior_tick{};
        batch.Take(drained, groups, prior_tick);
        pending.DrainReorderedUploads();
        pending.Record(); // RecordAs materializes the upload command before SubmitExecution.
        REQUIRE(batch.Empty());
        REQUIRE(drained.size() == 1);
        CHECK(drained.front().srcOffset == 4);
        CHECK(drained.front().dstOffset == 8);
        CHECK(drained.front().size == 4);
        REQUIRE(groups.size() == 1);
        CHECK(groups.front().src == source);
        CHECK(groups.front().dst == destination);
        CHECK(prior_tick == 17);
    }
    SECTION("In-order copies stay pending after being drained into a command") {
        Vulkan::UploadBatch batch;
        batch.Append(source, destination, copies, [] { FAIL("One copy cannot overflow a batch"); },
                     [&] { pending.QueueUploads(); });
        REQUIRE(pending.HasWork());
        REQUIRE(pending.UploadsPending());
        REQUIRE(batch.copies.size() == 1);
        batch.copies.clear();
        batch.groups.clear();
        pending.DrainUploads();
        pending.Record();
    }
    REQUIRE(pending.HasWork());
    REQUIRE_FALSE(pending.UploadsPending());
    REQUIRE_FALSE(pending.ReorderedUploadsPending());
    pending.Submitted();
    REQUIRE_FALSE(pending.HasWork());
    CHECK(pending.HasWork(true)); // A deferred clear is work before its command is realized too.
}

TEST_CASE("One upload span is bounded and overlapping writes stay ordered",
          "[video_core][upload_batch]") {
    Vulkan::UploadBatch batch;
    const auto source = reinterpret_cast<VkBuffer>(uintptr_t{1});
    const auto destination = reinterpret_cast<VkBuffer>(uintptr_t{2});
    std::array<u32, 66> source_words{};
    std::array<u32, 65> destination_words{};
    std::vector<VkBufferCopy> copies;
    for (u32 index = 0; index < source_words.size(); ++index) {
        source_words[index] = 100 + index;
        copies.push_back({index * sizeof(u32), (std::min)(index, u32{64}) * sizeof(u32),
                          sizeof(u32)});
    }
    std::vector<size_t> batch_sizes;
    size_t starts = 0;
    const auto flush = [&] {
        REQUIRE_FALSE(batch.Empty());
        REQUIRE(batch.copies.size() <= Vulkan::UploadBatch::MaxCopies);
        REQUIRE(batch.groups.size() == 1);
        REQUIRE(batch.groups.front().src == source);
        REQUIRE(batch.groups.front().dst == destination);
        batch_sizes.push_back(batch.copies.size());
        // Copies within one Vulkan command may run in any order. Exercise reverse order.
        for (auto it = batch.copies.rbegin(); it != batch.copies.rend(); ++it) {
            destination_words[it->dstOffset / sizeof(u32)] =
                source_words[it->srcOffset / sizeof(u32)];
        }
        batch.copies.clear();
        batch.groups.clear();
    };
    batch.Append(source, destination, copies, flush, [&] { ++starts; });
    flush();
    const std::vector<size_t> expected_sizes{64, 1, 1};
    REQUIRE(batch_sizes == expected_sizes);
    REQUIRE(starts == 3);
    for (size_t index = 0; index < 64; ++index) {
        REQUIRE(destination_words[index] == source_words[index]);
    }
    REQUIRE(destination_words.back() == source_words.back());
}

TEST_CASE("Dependent upload sources flush earlier writes before a new batch",
          "[video_core][upload_batch]") {
    Vulkan::UploadBatch batch;
    const auto first = reinterpret_cast<VkBuffer>(uintptr_t{1});
    const auto second = reinterpret_cast<VkBuffer>(uintptr_t{2});
    const auto third = reinterpret_cast<VkBuffer>(uintptr_t{3});
    const std::array<VkBufferCopy, 1> copies{{{0, 0, 4}}};
    std::vector<VkBuffer> flushed_sources;
    size_t starts = 0;
    const auto flush = [&] {
        REQUIRE(batch.groups.size() == 1);
        flushed_sources.push_back(batch.groups.front().src);
        batch.copies.clear();
        batch.groups.clear();
    };
    const auto begin = [&] { ++starts; };
    batch.Append(first, second, copies, flush, begin);
    batch.Append(second, third, copies, flush, begin);
    REQUIRE(flushed_sources == std::vector<VkBuffer>{first});
    REQUIRE_FALSE(batch.Empty());
    REQUIRE(batch.groups.front().src == second);
    REQUIRE(batch.groups.front().dst == third);
    REQUIRE(starts == 2);
}
