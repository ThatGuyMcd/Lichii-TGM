// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Vulkan {

/// Completion is queried without submitting work. Incomplete resources retain their ownership.
template <typename Container, typename IsComplete>
void DrainRetiredOwnership(Container& retired, IsComplete&& is_complete) {
    static_assert(std::is_nothrow_move_assignable_v<typename Container::value_type>);
    size_t index = 0;
    while (index < retired.size()) {
        if (is_complete(retired[index].tick)) {
            if (index + 1 != retired.size()) {
                retired[index] = std::move(retired.back());
            }
            retired.pop_back();
        } else {
            ++index;
        }
    }
}

/// Transfer must not run until a return slot exists: allocation failure leaves live handles with
/// their original owner. The transfer itself and moving the completed record cannot throw.
template <typename Container, typename IsComplete, typename Transfer>
void RetireOwnership(Container& retired, IsComplete&& is_complete, Transfer&& transfer) {
    using Record = typename Container::value_type;
    static_assert(std::is_nothrow_move_constructible_v<Record>);
    static_assert(std::is_nothrow_invocable_r_v<Record, Transfer>);
    if (retired.size() == retired.capacity()) {
        DrainRetiredOwnership(retired, is_complete);
    }
    if (retired.size() == retired.capacity()) {
        const size_t capacity = retired.capacity();
        if (capacity == retired.max_size()) {
            throw std::length_error{"Retirement storage exhausted"};
        }
        const size_t growth = (std::min)(capacity == 0 ? size_t{1} : capacity,
                                        retired.max_size() - capacity);
        retired.reserve(capacity + growth);
    }
    retired.push_back(std::forward<Transfer>(transfer)());
}

} // namespace Vulkan
