// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <mutex>
#include <new>
#include <type_traits>

namespace Tegra {

// Reuse ordinary command snapshots without retaining an unbounded guest submission backlog.
class CommandWordsPool {
public:
    static constexpr size_t MaxBlock = 4 * 1024 * 1024;
    static constexpr size_t MaxClassBytes = 8 * 1024 * 1024;

    static CommandWordsPool& Get() {
        // Qt's global System can release its last channel during static destruction. Retain the
        // bounded pool for process lifetime so those late vectors never reach a destroyed mutex.
        static CommandWordsPool* const pool = new CommandWordsPool;
        return *pool;
    }

    void* Allocate(size_t bytes) {
        const size_t index = ClassOf(bytes);
        if (index == Classes) {
            return ::operator new(bytes);
        }
        {
            std::scoped_lock lock{mutex};
            if (counts[index] != 0) {
                return blocks[index][--counts[index]];
            }
        }
        return ::operator new(BlockSize(index));
    }

    void Release(void* pointer, size_t bytes) noexcept {
        const size_t index = ClassOf(bytes);
        if (index != Classes) {
            std::scoped_lock lock{mutex};
            const size_t limit = std::min(BlocksPerClass, MaxClassBytes / BlockSize(index));
            if (counts[index] < limit) {
                blocks[index][counts[index]++] = pointer;
                return;
            }
        }
        ::operator delete(pointer);
    }

    ~CommandWordsPool() {
        for (size_t i = 0; i < Classes; ++i) {
            for (size_t j = 0; j < counts[i]; ++j) {
                ::operator delete(blocks[i][j]);
            }
        }
    }

private:
    static constexpr size_t Classes = 11;
    static constexpr size_t BlocksPerClass = 32;
    static constexpr size_t ClassOf(size_t bytes) {
        if (bytes > MaxBlock) {
            return Classes;
        }
        return bytes <= 4096 ? 0 : std::bit_width((bytes - 1) / 4096);
    }
    static constexpr size_t BlockSize(size_t index) {
        return size_t{4096} << index;
    }
    std::mutex mutex;
    std::array<std::array<void*, BlocksPerClass>, Classes> blocks{};
    std::array<size_t, Classes> counts{};
};

template <typename T>
class CommandWordsAllocator {
public:
    using value_type = T;
    using is_always_equal = std::true_type;
    CommandWordsAllocator() = default;
    template <typename U>
    CommandWordsAllocator(const CommandWordsAllocator<U>&) {}
    T* allocate(size_t count) {
        static_assert(alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__);
        if (count > std::numeric_limits<size_t>::max() / sizeof(T)) {
            throw std::bad_array_new_length{};
        }
        return static_cast<T*>(CommandWordsPool::Get().Allocate(count * sizeof(T)));
    }
    void deallocate(T* pointer, size_t count) noexcept {
        CommandWordsPool::Get().Release(pointer, count * sizeof(T));
    }
    bool operator==(const CommandWordsAllocator&) const = default;
};

} // namespace Tegra
