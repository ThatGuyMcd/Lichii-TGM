// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <catch2/catch_test_macros.hpp>
#include "video_core/front_qmd.h"
#include "video_core/gpu_split.h"
#include "video_core/native_qmd.h"

namespace VideoCommon {
struct Native60FrontQmdTestAccess {
    template <typename Memory>
    static auto Late(FrontQmdView& view, GpuSplit& split, const Memory& memory, u64 address) {
        const auto outcome = view.Attempt(split, memory, 0, address, 3, nullptr, true);
        return std::pair{outcome.valued, outcome.lost};
    }
    static void CopyTable(const FrontQmdView& view, Tegra::NativeQmdScheduler& destination) {
        destination.CopyStates(view.scheduler);
    }
};
} // namespace VideoCommon
using Tegra::NativeQmdScheduler;
using Address = NativeQmdScheduler::Address;
using Descriptor = NativeQmdScheduler::Descriptor;
using Word = NativeQmdScheduler::Word;
namespace {
constexpr Word Enable = 1U << 23;
constexpr Word OneWord = 1U << 30;
constexpr Word Reduction = 1U << 26;
Descriptor MakeQmd() {
    Descriptor qmd{};
    qmd[18] = 0x30;
    return qmd;
}
void SetRelease(Descriptor& qmd, Word base, Word address, Word flags, Word payload) {
    qmd[base] = address;
    qmd[base + 1] = flags;
    qmd[base + 2] = payload;
}
/// A GPU address space as the front's QMD view reads it (FrontQmdView's Memory): 4 KiB GPU pages
/// mapped to device pages, two GPU pages to one where `pages` aliases them, and every write sent
/// already run by the back.
struct AliasedMemory {
    std::map<Address, Address> pages; ///< GPU page to device page
    std::map<Address, std::array<std::uint8_t, 0x1000>> device;

    [[nodiscard]] std::optional<std::uint64_t> Device(Address address) const {
        const auto it = pages.find(address & ~Address{0xfff});
        if (it == pages.end()) {
            return std::nullopt;
        }
        return it->second + (address & 0xfff);
    }
    [[nodiscard]] bool IsMapped(Address address, std::uint64_t size) const {
        for (Address page = address & ~Address{0xfff}; page < address + size; page += 0x1000) {
            if (!pages.contains(page)) {
                return false;
            }
        }
        return true;
    }
    void Read(Address address, void* data, std::uint64_t size) const {
        const Address at = *Device(address); // Within one page here
        std::memcpy(data, device.at(at & ~Address{0xfff}).data() + (at & 0xfff), size);
    }
    [[nodiscard]] Word ReadWord(Address address) const {
        Word word;
        Read(address, &word, sizeof(word));
        return word;
    }
    [[nodiscard]] bool InSyncpointShim(Address, std::uint64_t) const {
        return false;
    }
    [[nodiscard]] bool GpuWritten(std::uint64_t, std::uint64_t) const {
        return false;
    }
    [[nodiscard]] std::uint64_t WritesTarget(const VideoCommon::GpuSplit&, Address,
                                             std::uint64_t) const {
        return 0;
    }
    void NoteWrite(VideoCommon::GpuSplit&, Address, std::uint64_t) const {}
    void NoteWriteAt(VideoCommon::GpuSplit&, Address, std::uint64_t, std::uint64_t) const {}
    [[nodiscard]] const void* Id() const {
        return this;
    }

    void Store(Address address, const Descriptor& qmd) {
        const Address at = *Device(address);
        std::memcpy(device[at & ~Address{0xfff}].data() + (at & 0xfff), qmd.data(), sizeof(qmd));
    }
};



} // namespace

TEST_CASE("Native60: late QMD releases use device granules and roll the attempt back",
          "[video_core][native_qmd]") {
    constexpr Address first = 0x10000;
    constexpr Address second = 0x11000;
    constexpr Address alias = 0x51000;
    for (const Address destination : {alias + 0x40, alias + 0x100}) {
        CAPTURE(destination);
        AliasedMemory memory;
        memory.pages = {{first, 0x7000}, {second, 0x9000}, {alias, 0x7000}};
        memory.Store(first, MakeQmd());
        auto release = MakeQmd();
        SetRelease(release, 24, Word(destination), Enable | OneWord, 7);
        memory.Store(second, release);
        auto split = std::make_unique<VideoCommon::GpuSplit>();
        auto view = std::make_unique<VideoCommon::FrontQmdView>();
        NativeQmdScheduler before{NativeQmdScheduler::OwnTable{}};
        view->TakeOver(before, VideoCommon::Nx2AcquireStats::QmdTakeOver::FrontStart);
        using Access = VideoCommon::Native60FrontQmdTestAccess;
        REQUIRE(Access::Late(*view, *split, memory, first).first);
        Access::CopyTable(*view, before);
        const auto [valued, reason] = Access::Late(*view, *split, memory, second);
        if (destination == alias + 0x40) {
            CHECK_FALSE(valued);
            CHECK(reason == VideoCommon::Nx2AcquireStats::QmdLost::LateReleasedInto);
            CHECK(view->SameTable(before));
        } else {
            CHECK(valued);
        }
    }
}
TEST_CASE("Native60: The front's QMD view finds a release into a QMD through another GPU address",
          "[video_core][native_qmd]") {
    // NX2 maps CPU ranges twice (nvhost_as_gpu): a launch's release through one GPU address can
    // write the dependent QMD the scheduler reads next through another. The back reads that QMD
    // with the release written; the front reads it as it was, so it must not value the signal.
    constexpr Address first = 0x10000;
    constexpr Address second = 0x11000; // Its dependent QMD, at the start of a device page
    constexpr Address before = 0x50000; // The GPU page before the alias, elsewhere in memory
    constexpr Address alias = 0x51000;  // Maps the dependent QMD's device page
    constexpr Address word = 0x20000;   // What the dependent QMD releases
    constexpr Word Increment = Enable | OneWord | Reduction;
    constexpr Word Timestamped = Enable; // Four words
    const std::map<Address, Address> pages{
        {first, 0x7000}, {second, 0x9000}, {before, 0xa000}, {alias, 0x9000}, {word, 0x8000}};
    const auto signal = [&](Address destination, Word flags) {
        AliasedMemory memory;
        memory.pages = pages;
        auto qmd = MakeQmd();
        SetRelease(qmd, 24, Word(destination), flags, 1);
        qmd[15] = Word(second >> 8);
        qmd[16] = 1 | (3U << 1); // Schedules the dependent QMD
        auto dependent = MakeQmd();
        SetRelease(dependent, 24, Word(word), Enable | OneWord, 7);
        memory.Store(first, qmd);
        memory.Store(second, dependent);
        memory.device[0x8000] = {};
        auto split = std::make_unique<VideoCommon::GpuSplit>();
        auto view = std::make_unique<VideoCommon::FrontQmdView>();
        const NativeQmdScheduler back{NativeQmdScheduler::OwnTable{}};
        view->TakeOver(back, VideoCommon::Nx2AcquireStats::QmdTakeOver::FrontStart);
        view->Signal(*split, memory, first, 3, nullptr);
        return view->Synced(); // Whether it valued the signal (it loses its table if not)
    };
    // Into the dependent QMD through its own address, through the alias, and through the page
    // before the alias (its timestamp lands in the QMD's first bytes).
    REQUIRE_FALSE(signal(second + 0x40, Increment));
    REQUIRE_FALSE(signal(alias + 0x40, Increment));
    REQUIRE_FALSE(signal(alias - 8, Timestamped));
    // Right past it, and right before it in the other page.
    REQUIRE(signal(alias + 0x100, Increment));
    REQUIRE(signal(alias - 0x10, Timestamped));
}
