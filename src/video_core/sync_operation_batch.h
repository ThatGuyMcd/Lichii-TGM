// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

namespace VideoCommon {

/// Owned records appended while the fence queue is locked, then immutable until Run/drop.
/// Append must copy its input before returning. A failed append must leave earlier records valid.
class SyncOperationBatch {
public:
    virtual ~SyncOperationBatch() = default;
    virtual void Append(const void* record) = 0;
    virtual void Run() = 0;
};

using SyncBatchFactory = std::shared_ptr<SyncOperationBatch> (*)(void* owner);

} // namespace VideoCommon
