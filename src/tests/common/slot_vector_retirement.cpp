// SPDX-FileCopyrightText: Copyright 2026 Lichii-TGM contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <new>

#include "common/slot_vector.h"

namespace {
bool refuse_allocation{};
constexpr int UnhandledAllocation = 42;
} // namespace

void* operator new(std::size_t bytes) {
    if (refuse_allocation) {
        throw std::bad_alloc{};
    }
    if (void* memory = std::malloc(bytes == 0 ? 1 : bytes)) {
        return memory;
    }
    throw std::bad_alloc{};
}

void* operator new[](std::size_t bytes) {
    return ::operator new(bytes);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

namespace {
template <std::size_t Count>
bool RetireAndReuse() {
    Common::SlotVector<u32> slots;
    std::array<Common::SlotId, Count> ids;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ids[i] = slots.insert(static_cast<u32>(i));
    }

    refuse_allocation = true;
    for (const auto id : ids) {
        slots.erase(id);
    }
    for (std::size_t i = ids.size(); i > 0; --i) {
        const auto value = static_cast<u32>(ids.size() + i);
        const auto replacement = slots.insert(value);
        if (replacement != ids[i - 1] || !slots.contains(replacement) ||
            slots[replacement] != value) {
            refuse_allocation = false;
            return false;
        }
    }
    refuse_allocation = false;
    return true;
}
} // namespace

int main() {
    std::set_terminate([] { std::_Exit(UnhandledAllocation); });
    if (!RetireAndReuse<1>() || !RetireAndReuse<8>() || !RetireAndReuse<257>()) {
        std::fputs("SlotVector failed to preserve freed slot order and values\n", stderr);
        return EXIT_FAILURE;
    }
    std::puts("SlotVector retired and reused all 266 slots without allocating");
    return EXIT_SUCCESS;
}
