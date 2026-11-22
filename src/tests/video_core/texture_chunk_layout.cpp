// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using u64 = std::uint64_t;
constexpr u64 operator""_MiB(unsigned long long value) { return value << 20; }
enum class ImageFlagBits : unsigned {
    None = 0, Sparse = 1, Converted = 2, AcceleratedUpload = 4, Unbacked = 8,
    TrimmedLevels = 16,
};
constexpr ImageFlagBits operator|(ImageFlagBits a, ImageFlagBits b) {
    return static_cast<ImageFlagBits>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
constexpr ImageFlagBits operator&(ImageFlagBits a, ImageFlagBits b) {
    return static_cast<ImageFlagBits>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
}
constexpr bool True(ImageFlagBits value) { return value != ImageFlagBits::None; }
enum class ImageType { e2D, e3D, Linear };
struct Info { ImageType type = ImageType::e2D; bool supported = true; int padding = -1; };
struct Copy { size_t subresource; };
struct Staging { std::span<std::uint32_t> mapped_span; };
struct Plan {
    bool layout_valid;
    std::vector<Copy> copies;
    size_t total_host_bytes = sizeof(std::uint32_t);
};
Plan PlanUnswizzleImageRows(u64, const Info& info,
                           const std::array<std::pair<u64, u64>, 1>& range) {
    const size_t index = range[0].first / 32_MiB;
    return {info.supported, static_cast<int>(index) == info.padding
                               ? std::vector<Copy>{} : std::vector<Copy>{{index}}};
}
struct Guest {
    std::array<std::uint32_t, 3> words{0x10203040, 0x55667788, 0xabcdef01};
    int fail = -1;
};
void ExecuteUnswizzlePlan(Guest& guest, const Plan& plan, std::span<std::uint32_t> out) {
    const auto index = plan.copies.at(0).subresource;
    if (static_cast<int>(index) == guest.fail) throw std::bad_alloc{};
    out[0] = guest.words.at(index);
}
struct Image {
    ImageFlagBits flags = ImageFlagBits::None;
    Info info;
    u64 gpu_addr = 0x10000;
    u64 guest_size_bytes = 96_MiB;
    bool initialized{};
    std::array<bool, 3> general{};
    std::array<std::uint32_t, 3> host{};
    int invalid_old_layouts{};
    int uploads{};

    void UploadMemory(const Staging& staging, const std::vector<Copy>& copies) {
        // Independent device boundary: Vulkan GENERAL must match each copied subresource.
        // Local Image::UploadMemory tracks initialization per image, whereas
        // CopyBufferToImage transitions only the mip/layer range in this chunk's copies.
        const bool prior = std::exchange(initialized, true);
        for (const auto& copy : copies) {
            if (prior && !general.at(copy.subresource)) ++invalid_old_layouts;
            general.at(copy.subresource) = true;
            host.at(copy.subresource) = staging.mapped_span[0];
        }
        ++uploads;
    }
};
struct Runtime {
    std::uint32_t scratch{};
    int requests{}, transitions{}, barriers{};
    int fail_request = -1;
    void TransitionImageLayout(Image& image) {
        if (!std::exchange(image.initialized, true)) {
            image.general.fill(true);
            ++transitions;
        }
    }
    Staging UploadStagingBuffer(size_t size) {
        if (requests++ == fail_request) throw std::bad_alloc{};
        if (size != sizeof(scratch)) throw std::runtime_error("unexpected plan size");
        return {{&scratch, 1}};
    }
    void InsertUploadMemoryBarrier() { ++barriers; }
};
struct Cache {
    Runtime runtime;
    Guest guest;
    Guest* gpu_memory = &guest;
    u64 MapSizeBytes(const Image& image) const { return image.guest_size_bytes; }
    bool UploadInChunks(Image& image) {
#include "texture_chunk_upload.inc"
    }
};
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int failures{};
template <typename F> void Case(const char* name, F&& function) {
    try { function(); std::printf("PASS %s\n", name); }
    catch (const std::exception& error) {
        ++failures;
        std::printf("FAIL %s: %s\n", name, error.what());
    }
}
} // namespace

int main() {
    Case("disjoint subresources have valid old layouts and exact guest words", [] {
        Cache cache; Image image;
        Require(cache.UploadInChunks(image), "chunk route not taken");
        Require(image.invalid_old_layouts == 0, "GENERAL used on an untouched subresource");
        Require(image.host == cache.guest.words, "uploaded words differ");
        Require(cache.runtime.transitions == 1 && cache.runtime.barriers == 1,
                "whole-image transition or final upload barrier missing");
    });
    Case("partial CPU allocation failure preserves untouched subresource layouts", [] {
        Cache cache; Image image; cache.guest.fail = 1;
        bool failed{};
        try { cache.UploadInChunks(image); } catch (const std::bad_alloc&) { failed = true; }
        Require(failed && image.uploads == 1, "expected partial upload and propagated failure");
        Require(std::ranges::all_of(image.general, [](bool value) { return value; }),
                "untouched subresources lack a defined layout after partial upload");
        Require(cache.runtime.barriers == 0, "failed upload reported complete");
        cache.guest.fail = -1;
        Require(cache.UploadInChunks(image), "retry route not taken");
        Require(image.host == cache.guest.words && image.invalid_old_layouts == 0,
                "retry did not replace all subresources correctly");
        Require(cache.runtime.transitions == 1, "retry discarded an initialized image");
    });
    Case("staging failure propagates before any copy", [] {
        Cache cache; Image image; cache.runtime.fail_request = 0;
        bool failed{};
        try { cache.UploadInChunks(image); } catch (const std::bad_alloc&) { failed = true; }
        Require(failed && image.uploads == 0 && cache.runtime.barriers == 0,
                "staging failure was swallowed or falsely completed");
    });
    Case("already initialized image is not discarded", [] {
        Cache cache; Image image; image.initialized = true; image.general.fill(true);
        Require(cache.UploadInChunks(image), "chunk route not taken");
        Require(cache.runtime.transitions == 0 && image.host == cache.guest.words,
                "existing contents were discarded or wrong words uploaded");
    });
    Case("padding subresource has a layout without a copy", [] {
        Cache cache; Image image; image.info.padding = 1;
        Require(cache.UploadInChunks(image), "chunk route not taken");
        Require(image.general[1] && image.host[1] == 0 && image.uploads == 2,
                "padding was copied or left with undefined layout");
        Require(image.invalid_old_layouts == 0, "last chunk used an undefined old layout");
    });
    Case("unsupported row layout returns to the whole-upload caller", [] {
        Cache cache; Image image; image.info.supported = false;
        Require(!cache.UploadInChunks(image), "unsupported layout incorrectly accepted");
        Require(image.uploads == 0 && cache.runtime.requests == 0 && cache.runtime.barriers == 0,
                "unsupported plan copied or falsely completed");
    });
    for (auto flags : {ImageFlagBits::Sparse, ImageFlagBits::Converted,
                       ImageFlagBits::AcceleratedUpload, ImageFlagBits::Unbacked,
                       ImageFlagBits::TrimmedLevels}) {
        Case("excluded image bypasses chunk initialization and allocations", [flags] {
            Cache cache; Image image; image.flags = flags;
            Require(!cache.UploadInChunks(image), "excluded image entered chunk route");
            Require(!image.initialized && cache.runtime.requests == 0,
                    "excluded image was touched");
        });
    }
    for (auto type : {ImageType::e3D, ImageType::Linear}) {
        Case("non-2D image keeps its existing upload path", [type] {
            Cache cache; Image image; image.info.type = type;
            Require(!cache.UploadInChunks(image) && !image.initialized,
                    "non-2D image was touched");
        });
    }
    Case("small images keep the whole-upload path", [] {
        Cache cache; Image image; image.guest_size_bytes = 64_MiB - 1;
        Require(!cache.UploadInChunks(image) && !image.initialized,
                "small image entered chunk route");
    });
    std::printf("14 cases, %d failures\n", failures);
    return failures != 0;
}
