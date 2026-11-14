// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <exception>
#include <memory>
#include "common/thread_worker.h"
#include "video_core/dma_pusher.h"

namespace Tegra {

// Every copy completes before parsing resumes. The worker owns its staging lists; neither the
// queue nor guest mappings may advance while Refresh is running on the GPU front.
class CommandSnapshotRefresher {
public:
    template <typename Read>
    static void Copy(CommandList& list, u64 stamp, size_t first, const Read& read) {
        for (size_t i = first; i < list.command_lists.size() && i < list.submitted_offsets.size();
             ++i) {
            const auto& entry = list.command_lists[i];
            const size_t offset = list.submitted_offsets[i];
            if (offset > list.submitted_words.size() ||
                entry.size > list.submitted_words.size() - offset) {
                continue;
            }
            read(entry.addr, list.submitted_words.data() + offset, entry.size * sizeof(u32));
        }
        list.deferred_entries.clear();
        list.snapshot_suspensions = stamp;
    }

    template <typename Read>
    void Refresh(std::deque<CommandList>& lists, u64 stamp, size_t first, const Read& read) {
        if (lists.size() > 256) {
            return Serial(lists, stamp, first, read);
        }
        size_t words{};
        size_t metadata{};
        for (const auto& list : lists) {
            const size_t entries = list.command_lists.size() + list.submitted_offsets.size() +
                                   list.deferred_entries.size() + list.submit_hashes.size() +
                                   list.prefetch_command_list.size();
            if (entries > MaxMetadata - metadata ||
                list.submitted_words.size() > MaxWords - words) {
                return Serial(lists, stamp, first, read);
            }
            metadata += entries;
            words += list.submitted_words.size();
        }
        if (lists.size() < 2 || words < 16384) {
            return Serial(lists, stamp, first, read);
        }
        // Copy the metadata and previous words too: entries preceding a suspended command must
        // retain their original snapshot. No partial refresh is published on failure.
        auto staging = std::make_shared<std::deque<CommandList>>(lists);
        if (!worker) {
            worker = std::make_unique<Common::ThreadWorker>(1, "Command snapshots");
        }
        std::exception_ptr background_error;
        worker->QueueWork([staging, stamp, read, &background_error] {
            try {
                for (size_t i = 1; i < staging->size(); ++i) {
                    Copy((*staging)[i], stamp, 0, read);
                }
            } catch (...) {
                background_error = std::current_exception();
            }
        });
        try {
            Copy(staging->front(), stamp, first, read);
        } catch (...) {
            worker->WaitForRequests();
            throw;
        }
        worker->WaitForRequests();
        if (background_error) {
            std::rethrow_exception(background_error);
        }
        for (size_t i = 0; i < lists.size(); ++i) {
            lists[i].submitted_words.swap((*staging)[i].submitted_words);
            lists[i].deferred_entries.clear();
            lists[i].snapshot_suspensions = stamp;
        }
    }

private:
    static constexpr size_t MaxWords = 8 * 1024 * 1024 / sizeof(CommandHeader);
    static constexpr size_t MaxMetadata = 65536;
    template <typename Read>
    static void Serial(std::deque<CommandList>& lists, u64 stamp, size_t first, const Read& read) {
        for (auto& list : lists) {
            Copy(list, stamp, first, read);
            first = 0;
        }
    }
    std::unique_ptr<Common::ThreadWorker> worker;
};

} // namespace Tegra
