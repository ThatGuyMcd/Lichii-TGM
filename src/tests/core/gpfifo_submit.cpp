// SPDX-FileCopyrightText: Copyright 2026 Lichii-TGM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include <boost/container/small_vector.hpp>
#include "common/bit_field.h"
#include "common/common_types.h"
#include "video_core/command_words_pool.h"

namespace {
thread_local bool count_allocations = false;
thread_local size_t allocations = 0;
}
void* operator new(size_t bytes) {
    if (count_allocations) ++allocations;
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc{};
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

namespace Tegra {
enum class SubmissionMode : u32 {};
#include "gpfifo_command_types.inc"
}
namespace Service::Nvidia::Devices {
enum class NvResult { Success, InvalidSize };
}
#include "gpfifo_serialization.inc"
using namespace Service::Nvidia::Devices;
using DeviceFD = int;
struct Ioctl { unsigned cmd; };
struct NvFence { u32 id{}; u32 value{}; };
#include "gpfifo_constants.inc"
#define UNIMPLEMENTED() ((void)0)

void Require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
struct nvhost_gpu {
#include "gpfifo_fixed.inc"
    static_assert(sizeof(IoctlSubmitGpfifo) == 24);
    std::optional<Tegra::CommandList> submitted;
    unsigned logs{}, reads{};
    bool unmapped{}, nested{};
    struct Memory {
        nvhost_gpu* owner;
        void ReadBlock(u64, void* output, size_t bytes) {
            ++owner->reads;
            auto* p = static_cast<u8*>(output);
            for (size_t i = 0; i < bytes; ++i) p[i] = owner->unmapped ? 0 : u8(i * 17 + 3);
        }
    } memory{this};
    Memory& GetSessionMemory(DeviceFD) { return memory; }
    void LogGPFIFOSubmit(DeviceFD, Ioctl, int, std::span<const u8>, size_t,
                         std::span<u8>, NvResult) { ++logs; }
    NvResult SubmitGPFIFOImpl(IoctlSubmitGpfifo& params, Tegra::CommandList&& entries) {
        if (nested) {
            nested = false;
            const auto before = entries.command_lists.empty() ? 0 : entries.command_lists.front().raw;
            std::array<u8, 32> input{};
            input[8] = 1; input[24] = 0xe7;
            std::array<u8, 24> output{};
            Require(Ioctl1(0, {8}, input, output) == NvResult::Success, "nested submit failed");
            Require(entries.command_lists.empty() || entries.command_lists.front().raw == before,
                    "nested submit overwrote outer commands");
        }
        params.fence.value = 0x87654321;
        submitted.emplace(std::move(entries));
        return NvResult::Success;
    }
    NvResult SubmitGPFIFOBase1(IoctlSubmitGpfifo& params,
                              std::span<Tegra::CommandListHeader> commands, DeviceFD fd,
                              bool kickoff) {
#include "gpfifo_base1.inc"
    }
    NvResult SubmitGPFIFOBase2(IoctlSubmitGpfifo& params,
                              std::span<const Tegra::CommandListHeader> commands) {
#include "gpfifo_base2.inc"
    }
    NvResult Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input, std::span<u8> output) {
#include "gpfifo_route1.inc"
    }
    NvResult Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                   std::span<const u8> inline_input, std::span<u8> output) {
#include "gpfifo_route2.inc"
    }
};

struct Result { unsigned cases{}, failures{}; } result;
template <class F> void Run(const char* name, F test) {
    ++result.cases;
    try { test(); }
    catch (const std::exception& e) {
        count_allocations = false; ++result.failures;
        std::cerr << "FAIL " << name << ": " << e.what() << '\n';
    }
}

void Check(unsigned route, size_t count, size_t tail, size_t fixed_size, size_t output_size,
           bool alias, bool invalid, bool nested = false, bool unmapped = false) {
    const size_t input_size = fixed_size + (route == 2 ? 0 : count * 8 + tail);
    const size_t extent = std::max(input_size, output_size) + 16;
    std::vector<u8> input(extent, 0xa5), inlined(count * 8 + tail, 0x6e);
    std::array<u8, 24> fixed{};
    const u32 requested = static_cast<u32>(count + (invalid ? 1 : 0));
    std::memcpy(fixed.data() + 8, &requested, 4);
    std::copy_n(fixed.begin(), fixed_size, input.begin());
    for (size_t i = 0; i < count * 8 + tail; ++i) {
        const u8 value = u8(i * 31 + 11);
        if (route == 2) inlined[i] = value;
        else input[fixed_size + i] = value;
    }
    std::vector<u8> output(output_size + 16, 0xcd);
    auto out = alias ? std::span{input}.first(output_size) : std::span{output}.first(output_size);
    auto expected = alias ? input : output;
    const auto original = input;
    // Independent byte-level ABI oracle: zero-pad the fixed header and retain only
    // complete eight-byte entries; the submit boundary changes fence.value only.
    std::array<u8, 24> header{};
    std::copy_n(original.begin(), std::min(input_size, size_t{24}), header.begin());
    const size_t variable_start = std::min(input_size, size_t{24});
    const size_t available = route == 2 ? inlined.size() / 8 : (input_size - variable_start) / 8;
    u32 actual_count{};
    std::memcpy(&actual_count, header.data() + 8, 4);
    const bool accepted = actual_count <= available;
    if (accepted) { header[20] = 0x21; header[21] = 0x43; header[22] = 0x65; header[23] = 0x87; }
    for (size_t i = 0; i < std::min(output_size, size_t{24}); ++i) expected[i] = header[i];
    if (route != 2) {
        for (size_t i = 0; i < available * 8 && variable_start + i < output_size; ++i)
            expected[variable_start + i] = original[variable_start + i];
    }
    nvhost_gpu device; device.nested = nested; device.unmapped = unmapped;
    allocations = 0; count_allocations = true;
    const auto status = route == 2 ? device.Ioctl2(7, {0x1b}, std::span{input}.first(input_size), inlined, out)
                                  : device.Ioctl1(7, {route == 1 ? 8U : 0x1bU}, std::span{input}.first(input_size), out);
    count_allocations = false;
    const size_t observed_allocations = allocations;
    Require((status == NvResult::Success) == accepted, "return code differs from ABI oracle");
    Require((alias ? input : output) == expected, "output bytes or canary changed");
    Require(device.logs == (nested && accepted ? 2U : 1U), "submit logging lost");
    Require(bool(device.submitted) == accepted, "invalid request reached submit boundary");
    if (accepted) {
        Require(device.submitted->command_lists.size() == actual_count, "submitted length changed");
        const auto* bytes = reinterpret_cast<const u8*>(device.submitted->command_lists.data());
        for (size_t i = 0; i < actual_count * 8; ++i) {
            const u8 wanted = route == 3 ? (unmapped ? 0 : u8(i * 17 + 3)) :
                              route == 2 ? inlined[i] : original[variable_start + i];
            Require(bytes[i] == wanted, "submitted command bytes changed");
        }
        Require(device.reads == (route == 3 ? 1U : 0U), "guest-read routing changed");
    }
    if (available <= 512 && actual_count <= 512)
        Require(observed_allocations == 0, "small submit allocated temporary storage");
    if (available > 512) Require(observed_allocations > 0, "growth control did not allocate");
}

int main() {
    for (unsigned route : {1U, 2U, 3U})
        for (size_t count : {size_t{0}, size_t{1}, size_t{511}, size_t{512}, size_t{513}, size_t{1024}})
            for (size_t tail : {size_t{0}, size_t{1}, size_t{7}})
                for (bool alias : {false, true})
                    Run("boundary/growth byte equivalence", [=] { Check(route, count, tail, 24, 24 + count * 8 + 3, alias, false); });
    for (unsigned route : {1U, 2U})
        for (size_t length = 0; length <= 24; ++length)
            for (size_t output : {size_t{0}, size_t{1}, size_t{23}, size_t{24}, size_t{37}})
                Run("short fixed input/output", [=] { Check(route, 0, 0, length, output, false, false); });
    for (unsigned route : {1U, 2U, 3U}) {
        Run("invalid entry count retains output ABI", [=] { Check(route, 1, 7, 24, 39, true, true); });
        Run("nested submit owns outer inline storage", [=] { Check(route, 1, 0, 24, 32, false, false, true); });
    }
    Run("unmapped kickoff zero fills", [] { Check(3, 512, 0, 24, 24, false, false, false, true); });
    Run("pooled submitted words retain deferred zero contract", [] {
        using Words = std::vector<Tegra::CommandHeader, Tegra::CommandWordsAllocator<Tegra::CommandHeader>>;
        void* released{};
        {
            Words words(64);
            for (auto& word : words) word.argument = 0xdeadbeef;
            words.clear(); words.resize(64);
            for (const auto& word : words) Require(word.argument == 0, "deferred slot lost zero initialization");
            for (auto& word : words) word.argument = 0xdeadbeef;
            released = words.data();
        }
        Words recycled(64);
        Require(recycled.data() == released, "bounded command storage pool was bypassed");
        for (const auto& word : recycled) Require(word.argument == 0, "recycled deferred slot contains stale bytes");
    });
    Run("ordinary ioctl inputs and inline outputs preserve behavior", [] {
        struct Fixed { u32 value; };
        std::array<u8, 20> input{};
        std::array<u8, 8> inline_input{};
        std::array<u8, 24> output{};
        std::array<u8, 6> inline_output{};
        input[0] = 7; input[4] = 11; input[12] = 13;
        inline_input[0] = 17; inline_input[4] = 19;
        output.fill(0xa5);
        const auto status = WrapGeneric<Fixed, u64, u32, u16>(
            [&](auto& fixed, auto& var, auto& inl_in, auto& inl_out) {
                Require(fixed.value == 7 && var.size() == 2 && var[0] == 11 && var[1] == 13,
                        "ordinary variable inputs changed");
                Require(inl_in.size() == 2 && inl_in[0] == 17 && inl_in[1] == 19,
                        "ordinary inline inputs changed");
                Require(inl_out.size() == 3 && inl_out[0] == 0 && inl_out[2] == 0,
                        "inline outputs lost initialization");
                fixed.value = 23; var[1] = 29; inl_out[2] = 31;
                return NvResult::InvalidSize;
            }, input, inline_input, output, inline_output);
        Require(status == NvResult::InvalidSize && output[0] == 23 && output[4] == 11 &&
                output[12] == 29 && output[20] == 0xa5 && inline_output[4] == 31,
                "error return did not preserve output serialization");
    });
    std::cout << result.cases << " cases, " << result.failures << " failures\n";
    return result.failures ? 1 : 0;
}
