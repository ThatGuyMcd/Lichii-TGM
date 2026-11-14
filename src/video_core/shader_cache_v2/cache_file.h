// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "common/common_types.h"
#include "common/fs/file.h"
#include "video_core/shader_cache_v2/cache_format.h"

namespace Common::FS {
class FileLock;
}

namespace VideoCommon::ShaderCacheV2 {

/// A log read into memory; `parsed` points into `bytes`.
struct LogFile {
    std::vector<u8> bytes;
    ParsedLog parsed;

    LogFile() = default;
    LogFile(LogFile&&) = default;
    LogFile& operator=(LogFile&&) = default;
    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;
};

/// Reads the whole log without its lock: records are only appended under it, and the reader
/// ignores one cut off by an append still in progress. A missing file reads as empty. See ParseLog
/// for `for_each`.
[[nodiscard]] LogFile ReadLogFile(const std::filesystem::path& file, const ForEach& for_each = {});

/// The payload of the record of `kind` and `key` that a read of the log found at `offset`, read
/// again from the file the same way. Empty when the file no longer holds it there intact (a
/// compaction rewrote it).
[[nodiscard]] std::optional<std::vector<u8>> ReadRecordPayload(const std::filesystem::path& file,
                                                               RecordKind kind, u64 key,
                                                               u64 offset, u64 payload_size);

enum class WriteResult {
    Written,
    Busy,         ///< Another process holds the log's lock
    Failed,       ///< A write failed; the file is as it was
    OtherVersion, ///< The file is another format version's, and stays as it is
    Exists,       ///< CreateLog found the file there already
};

/// Appends encoded records (EncodeRecord) holding the log's lock, which is tried once. The file
/// and its header are created when missing, a damaged header is written again, and a record cut
/// off by a crash is padded to the next record boundary first.
[[nodiscard]] WriteResult AppendToLog(const std::filesystem::path& file,
                                      std::span<const u8> records);

/// Bounds what CompactLog keeps. While the live records take more than `max_bytes` (as written
/// again), only `target_bytes` of them are kept: those of the lowest rank first, the newest first
/// within a rank. The kept records stay in the order of the file.
struct CompactLimit {
    u64 max_bytes{~u64{0}};
    u64 target_bytes{~u64{0}};
    std::function<u32(const LogRecord&)> rank;
};

/// Rewrites the log with only its live records: to a temporary file that is committed to the disk
/// and renamed over it. Holding the log's lock, tried once, so that it never runs while another
/// process reads it under the lock or writes it.
[[nodiscard]] WriteResult CompactLog(const std::filesystem::path& file,
                                     const CompactLimit& limit = {});

/// Produces a log one record at a time under its lock, publishing only after Commit.
class LogBuilder {
public:
    LogBuilder();
    ~LogBuilder();
    LogBuilder(const LogBuilder&) = delete;
    LogBuilder& operator=(const LogBuilder&) = delete;

    [[nodiscard]] WriteResult Start(const std::filesystem::path& file);
    bool Append(RecordKind kind, u64 key, std::span<const u8> payload);
    [[nodiscard]] WriteResult Commit();
    void Discard();

private:
    std::filesystem::path file;
    std::filesystem::path temporary;
    std::unique_ptr<Common::FS::FileLock> lock;
    Common::FS::IOFile target;
    std::vector<u8> buffer;
    bool written{true};
};

[[nodiscard]] u64 UnixNow();

} // namespace VideoCommon::ShaderCacheV2
