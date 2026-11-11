// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <new>
#include <optional>
#include <utility>

namespace Core {

// Optional working-set maintenance may run while memory is scarce. Failure to allocate its
// temporary lists must not escape the worker thread and terminate the process.
template <typename Trim>
std::optional<std::pair<size_t, size_t>> TryTrimZeroPages(Trim&& trim) {
    try {
        return std::forward<Trim>(trim)();
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
}

} // namespace Core
