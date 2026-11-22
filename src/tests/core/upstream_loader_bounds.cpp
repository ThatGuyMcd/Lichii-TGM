// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/common_funcs.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/file_sys/kernel_executable.h"
#include "core/file_sys/program_metadata.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"
#include "core/loader/kip.h"
#include "core/loader/nro.h"
#include "core/loader/nso.h"
#include "tests/core/system_test_context.h"

namespace {
template <class T>
void Put(std::vector<u8>& bytes, size_t offset, const T& value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

FileSys::VirtualFile File(std::vector<u8> bytes) {
    return std::make_shared<FileSys::VectorVfsFile>(std::move(bytes), "synthetic");
}

// The header read succeeds, but a segment read returns fewer bytes than GetSize promised.
class ShortReadFile : public FileSys::VectorVfsFile {
public:
    explicit ShortReadFile(std::vector<u8> bytes) : VectorVfsFile(std::move(bytes)) {}
    size_t Read(u8* out, size_t length, size_t offset) const override {
        return VectorVfsFile::Read(out, offset >= sizeof(Loader::NSOHeader) && length ? length - 1 : length,
                                   offset);
    }
};

class ObservedNsoFile : public FileSys::VectorVfsFile {
public:
    explicit ObservedNsoFile(std::vector<u8> bytes) : VectorVfsFile(std::move(bytes)) {}

    size_t Read(u8* out, size_t length, size_t offset) const override {
        if (offset == sizeof(Loader::NSOHeader)) {
            text_read_size = length;
        }
        return VectorVfsFile::Read(out, length, offset);
    }

    mutable size_t text_read_size{};
};

Loader::NSOHeader NsoHeader() {
    Loader::NSOHeader header{};
    header.magic = Common::MakeMagic('N', 'S', 'O', '0');
    for (size_t i = 0; i < 3; ++i) {
        header.segments[i] = {static_cast<u32>(sizeof(header) + i * 16),
                              static_cast<u32>(i * 0x1000), 16, {0}};
        header.segments_compressed_size[i] = 16;
    }
    header.segments[2].bss_size = 0x1000;
    return header;
}

std::vector<u8> NsoBytes(const Loader::NSOHeader& header, std::vector<u8> text = {}) {
    std::vector<u8> bytes(sizeof(header) + 48, 0x42);
    Put(bytes, 0, header);
    if (!text.empty()) {
        std::memcpy(bytes.data() + sizeof(header), text.data(), text.size());
    }
    return bytes;
}

template <class F>
void WithProcess(F&& run) {
    ScopedSystemTestContext environment;
    const auto args = Settings::values.program_args.GetValue();
    const auto backend = Settings::values.cpu_backend.GetValue();
    const bool nce = Settings::IsNceEnabled();
    SCOPE_EXIT {
        Settings::values.program_args.SetValue(args);
        Settings::values.cpu_backend.SetValue(backend);
        Settings::SetNceEnabled(nce);
    };
    Settings::values.program_args.SetValue("");
    Settings::values.cpu_backend.SetValue(Settings::CpuBackend::Dynarmic);
    Settings::SetNceEnabled(false);
    Core::System system;
    REQUIRE(system.Initialize());
    auto& kernel = system.Kernel();
    Kernel::KThread bootstrap{kernel};
    REQUIRE(Kernel::KThread::InitializeDummyThread(system, &bootstrap, nullptr).IsSuccess());
    kernel.SetCurrentEmuThread(&bootstrap);
    kernel.Initialize();
    SCOPE_EXIT {
        system.CoreTiming().ClearPendingEvents();
        kernel.Shutdown();
        kernel.SetCurrentEmuThread(nullptr);
        kernel.UnregisterKernelObject(&bootstrap);
    };
    Kernel::KProcess process{kernel};
    Kernel::KAutoObject::Create(&process);
    SCOPE_EXIT { kernel.UnregisterKernelObject(&process); };
    run(process, system);
}

constexpr u32 NroFileSize = 0x2800;
constexpr u32 NroImageSize = 0x4000;

std::vector<u8> NroBytes() {
    std::vector<u8> bytes(0x3000, 0x77);
    std::fill_n(bytes.begin(), NroFileSize, u8{});
    Put(bytes, 0x10, Common::MakeMagic('N', 'R', 'O', '0'));
    Put(bytes, 0x18, NroFileSize);
    Put(bytes, 0x24, u32{0x1000});
    Put(bytes, 0x28, u32{0x1000});
    Put(bytes, 0x2C, u32{0x1000});
    Put(bytes, 0x30, u32{0x2000});
    Put(bytes, 0x34, u32{0x800});
    Put(bytes, 0x38, u32{0x1000});
    bytes[0x1800] = 0x62;
    bytes[NroFileSize - 1] = 0x63;
    return bytes;
}

class ObservedNroFile : public FileSys::VectorVfsFile {
public:
    explicit ObservedNroFile(bool short_read)
        : VectorVfsFile(NroBytes(), "synthetic.nro"), short_read{short_read} {}

    size_t Read(u8* out, size_t length, size_t offset) const override {
        if (offset == 0 && length > 0x80) {
            program_read_size = length;
            if (short_read) {
                --length;
            }
        }
        return VectorVfsFile::Read(out, length, offset);
    }

    mutable size_t program_read_size{};

private:
    bool short_read;
};

std::vector<u8> Npdm(bool switch2) {
    const size_t aci = switch2 ? 0x500 : 0x400;
    const size_t acid_magic = switch2 ? 0x400 : 0x300;
    std::vector<u8> bytes(aci + 0x80);
    std::memcpy(bytes.data(), "META", 4);
    Put(bytes, 0x70, static_cast<u32>(aci));
    Put(bytes, 0x74, u32{0x80});
    Put(bytes, 0x78, u32{0x100});
    Put(bytes, 0x7C, switch2 ? u32{0x340} : u32{0x240});
    std::memcpy(bytes.data() + acid_magic, "ACID", 4);
    Put(bytes, acid_magic + 0x20, switch2 ? u32{0x340} : u32{0x240});
    Put(bytes, acid_magic + 0x24, u32{0x2C});
    std::memcpy(bytes.data() + aci, "ACI0", 4);
    Put(bytes, aci + 0x20, u32{0x40});
    Put(bytes, aci + 0x24, u32{0x1C});
    Put(bytes, aci + 0x30, u32{0x60});
    Put(bytes, aci + 0x34, u32{8});
    return bytes;
}

std::vector<u8> Blz() {
    // Eight literals and three copies; the decoded bytes are ABCDEFGH repeated four times.
    std::vector<u8> bytes{5, 0x50, 5, 0x50, 5, 0x50, 0xFF};
    for (const char c : std::string_view{"ABCDEFGH"}) {
        bytes.push_back(static_cast<u8>(c));
    }
    bytes.push_back(0);
    bytes.resize(28);
    Put(bytes, 16, u32{28});
    Put(bytes, 20, u32{12});
    Put(bytes, 24, u32{4});
    return bytes;
}

std::vector<u8> KipBytes(const std::vector<u8>& payload, u32 decompressed = 32, u32 stored = 28,
                         u32 bss = 0, bool compressed = true, u64 title_id = 1) {
    FileSys::KIPHeader header{};
    header.magic = Common::MakeMagic('K', 'I', 'P', '1');
    header.flags = compressed ? 1 : 0;
    header.title_id = title_id;
    header.sections[0].decompressed_size = decompressed;
    header.sections[0].compressed_size = stored;
    header.sections[1].offset = 0x1000;
    header.sections[2].offset = 0x1000;
    header.sections[3].offset = 0x2000;
    header.sections[3].decompressed_size = bss;
    std::vector<u8> bytes(sizeof(header));
    Put(bytes, 0, header);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}
} // namespace

TEST_CASE("Upstream NSO bounds reject corrupt images before loading", "[core][loader_bounds]") {
    WithProcess([](Kernel::KProcess& process, Core::System& system) {
        const auto load = [&](FileSys::VirtualFile file, bool arguments = false) {
            return Loader::AppLoader_NSO::LoadModule(process, system, *file, 0, arguments, false);
        };
        auto header = NsoHeader();
        REQUIRE(load(File(NsoBytes(header))) == 0x4000);
        SECTION("furthest segment determines the image extent") {
            header.segments[0].location = 0x6000;
            CHECK(load(File(NsoBytes(header))) == 0x8000);
        }
        SECTION("stored size cannot be smaller than its segment") {
            header.segments_compressed_size[0] = 15;
            CHECK_FALSE(load(File(NsoBytes(header))));
        }
        SECTION("unrepresentable segment and BSS extents") {
            header.segments[0].location = 0xFFFFFFF0;
            header.segments[0].size = 32;
            header.segments_compressed_size[0] = 32;
            CHECK_FALSE(load(File(NsoBytes(header))));
            header = NsoHeader();
            header.segments[2].bss_size = 0xFFFFFFFF;
            CHECK_FALSE(load(File(NsoBytes(header))));
        }
        SECTION("file offsets and declared sizes checked before allocation") {
            header.segments[0].offset = 0xFFFFFFF0;
            CHECK_FALSE(load(File(NsoBytes(header))));
            header = NsoHeader();
            header.flags = 1;
            header.segments_compressed_size[0] = 0xFFFFFFFF;
            CHECK_FALSE(load(File(NsoBytes(header))));
            header.segments_compressed_size[0] = 16;
            header.segments[0].size = 0x80000000;
            CHECK_FALSE(load(File(NsoBytes(header))));
        }
        SECTION("short VFS read is not accepted as a full segment") {
            CHECK_FALSE(load(std::make_shared<ShortReadFile>(NsoBytes(header))));
        }
        SECTION("impossible compressed expansion is rejected before reading the segment") {
            header.flags = 1;
            header.segments_compressed_size[0] = 1;
            header.segments[0].size = 256;
            const auto file = std::make_shared<ObservedNsoFile>(NsoBytes(header));
            CHECK_FALSE(load(file));
            CHECK(file->text_read_size == 0);
        }
        SECTION("stored padding is not copied into the image") {
            header.segments[0].size = 12;
            const auto file = std::make_shared<ObservedNsoFile>(NsoBytes(header));
            CHECK(load(file) == 0x4000);
            CHECK(file->text_read_size == 12);
        }
        SECTION("LZ4 must produce the complete declared segment") {
            header.flags = 1;
            header.segments_compressed_size[0] = 18;
            std::vector<u8> text{0xF0, 1};
            text.resize(18, 0x55);
            CHECK(load(File(NsoBytes(header, text))) == 0x4000);
            header.segments_compressed_size[0] = 5;
            CHECK_FALSE(load(File(NsoBytes(header, {0x40, 1, 2, 3, 4}))));
            header.segments_compressed_size[0] = 1;
            CHECK_FALSE(load(File(NsoBytes(header, {0xF0}))));
        }
        SECTION("arguments fit exactly in the reserved block") {
            Settings::values.program_args.SetValue(std::string(
                Loader::NSO_ARGUMENT_DATA_ALLOCATION_SIZE - sizeof(Loader::NSOArgumentHeader), 'a'));
            CHECK(load(File(NsoBytes(header)), true) == 0xD000);
            Settings::values.program_args.SetValue(Settings::values.program_args.GetValue() + 'a');
            CHECK_FALSE(load(File(NsoBytes(header)), true));
        }
    });
}

TEST_CASE("NRO layout limits page extents and treats empty sections explicitly",
          "[core][loader_bounds]") {
    auto bytes = NroBytes();
    bytes.resize(NroFileSize);
    const auto valid = Loader::ValidateNroLayout(*File(bytes));
    REQUIRE(valid);
    CHECK(valid->image_size == NroImageSize);
    CHECK(valid->segments[2].offset == 0x2000);
    CHECK(valid->segments[2].size == 0x2000);

    SECTION("a missing data section maps only BSS") {
        Put(bytes, 0x30, u32{0xFFFFFFFF});
        Put(bytes, 0x34, u32{0});
        const auto layout = Loader::ValidateNroLayout(*File(bytes));
        REQUIRE(layout);
        CHECK(layout->segments[2].offset == 0x3000);
        CHECK(layout->segments[2].size == 0x1000);
    }
    SECTION("invalid MOD0 falls back to the declared BSS") {
        Put(bytes, 4, u32{NroFileSize - 1});
        auto layout = Loader::ValidateNroLayout(*File(bytes));
        REQUIRE(layout);
        CHECK_FALSE(layout->ignored_module_header.empty());
        CHECK(layout->image_size == NroImageSize);
        Put(bytes, 4, u32{0x100});
        Put(bytes, 0x100, Common::MakeMagic('M', 'O', 'D', '0'));
        Put(bytes, 0x108, u32{0x2000});
        Put(bytes, 0x10C, u32{0x1FFF});
        layout = Loader::ValidateNroLayout(*File(bytes));
        REQUIRE(layout);
        CHECK_FALSE(layout->ignored_module_header.empty());
        CHECK(layout->image_size == NroImageSize);
        Put(bytes, 0x10C, u32{0x4000});
        layout = Loader::ValidateNroLayout(*File(bytes));
        REQUIRE(layout);
        CHECK(layout->ignored_module_header.empty());
        CHECK(layout->image_size == 0x5000);
    }
    SECTION("invalid program extents fail layout validation") {
        for (const auto [offset, value] :
             {std::pair{0x18U, NroFileSize + 1}, std::pair{0x28U, 0U},
              std::pair{0x30U, 0x1FFFU}, std::pair{0x34U, 0x801U},
              std::pair{0x38U, 0xFFFFFFFFU}}) {
            auto invalid = bytes;
            Put(invalid, offset, value);
            CHECK_FALSE(Loader::ValidateNroLayout(*File(std::move(invalid))));
        }
    }
}

TEST_CASE("NRO loader copies exactly the program bytes and preserves argument placement",
          "[core][loader_bounds]") {
    WithProcess([](Kernel::KProcess& process, Core::System& system) {
        const auto short_file = std::make_shared<ObservedNroFile>(true);
        Loader::AppLoader_NRO incomplete{short_file};
        CHECK(incomplete.Load(process, system).first == Loader::ResultStatus::ErrorLoadingNRO);
        CHECK(short_file->program_read_size == NroFileSize);

        Settings::values.program_args.SetValue("--synthetic");
        const auto file = std::make_shared<ObservedNroFile>(false);
        Loader::AppLoader_NRO loader{file};
        REQUIRE(loader.Load(process, system).first == Loader::ResultStatus::Success);
        CHECK(file->program_read_size == NroFileSize);
        constexpr u32 ArgvOffset = NroImageSize + 4 * 0x18;
        constexpr std::string_view Arguments{"homebrew --synthetic\0", 21};
        std::vector<u8> image(ArgvOffset + Arguments.size());
        process.GetMemory().ReadBlock(process.GetEntryPoint(), image.data(), image.size());
        CHECK(image[0x1800] == 0x62);
        CHECK(image[NroFileSize - 1] == 0x63);
        CHECK(std::all_of(image.begin() + NroFileSize, image.begin() + NroImageSize,
                          [](u8 byte) { return byte == 0; }));
        CHECK(std::string_view{reinterpret_cast<const char*>(image.data() + ArgvOffset),
                               Arguments.size()} == Arguments);
    });
}

TEST_CASE("NPDM validates ACI0 without losing either ACID layout", "[core][loader_bounds]") {
    for (const bool switch2 : {false, true}) {
        CAPTURE(switch2);
        const auto bytes = Npdm(switch2);
        FileSys::ProgramMetadata valid;
        CHECK(valid.Load(File(bytes)) == Loader::ResultStatus::Success);
        for (const u32 offset : {0U, 1U, 0x100U}) {
            auto corrupt = bytes;
            Put(corrupt, 0x70, offset);
            FileSys::ProgramMetadata invalid;
            CHECK(invalid.Load(File(std::move(corrupt))) == Loader::ResultStatus::ErrorBadACIHeader);
        }
    }
}

TEST_CASE("KIP parser bounds compressed input and leaves BSS virtual", "[core][loader_bounds]") {
    const FileSys::KIP valid{File(KipBytes(Blz()))};
    REQUIRE(valid.GetStatus() == Loader::ResultStatus::Success);
    const auto text = valid.GetSectionDecompressed(0);
    CHECK(std::string_view{reinterpret_cast<const char*>(text.data()), text.size()} ==
          "ABCDEFGHABCDEFGHABCDEFGHABCDEFGH");
    for (const u32 size : {29U, 0xFFFFFFFFU}) {
        const FileSys::KIP truncated{File(KipBytes(Blz(), 32, size))};
        CHECK(truncated.GetStatus() == Loader::ResultStatus::ErrorBadKIPHeader);
    }
    const FileSys::KIP stored{File(KipBytes(std::vector<u8>(16, 0x66), 32, 16, 0x2300, false))};
    REQUIRE(stored.GetStatus() == Loader::ResultStatus::Success);
    CHECK(stored.GetTextSection().size() == 16);
    CHECK(stored.GetBSSSize() == 0x2300);
    CHECK(stored.GetImageSize() == 0x5000);
    CHECK(stored.GetSectionDecompressed(3).empty());
    const FileSys::KIP oversized_bss{File(KipBytes({}, 32, 0, 0xFFFFFFF0, false))};
    CHECK(oversized_bss.GetStatus() == Loader::ResultStatus::ErrorBadKIPHeader);
    const FileSys::KIP oversized_raw{File(KipBytes(std::vector<u8>(17), 16, 17, 0, false))};
    CHECK(oversized_raw.GetStatus() == Loader::ResultStatus::ErrorBadKIPHeader);
    for (const auto [offset, value] : {std::pair{16U, 0x1000U}, std::pair{20U, 29U},
                                     std::pair{24U, 0xFFFFFFF0U}, std::pair{24U, 0xFFFFFFE4U}}) {
        auto corrupt = Blz();
        Put(corrupt, offset, value);
        const FileSys::KIP bad{File(KipBytes(corrupt))};
        CHECK(bad.GetStatus() == Loader::ResultStatus::ErrorBLZDecompressionFailed);
    }
    auto exhausted = Blz();
    exhausted[6] = 0;
    const FileSys::KIP underflow{File(KipBytes(exhausted))};
    CHECK(underflow.GetStatus() == Loader::ResultStatus::ErrorBLZDecompressionFailed);
    const FileSys::KIP over_expansion{File(KipBytes(Blz(), 31))};
    CHECK(over_expansion.GetStatus() == Loader::ResultStatus::ErrorBLZDecompressionFailed);
}

TEST_CASE("INI1 advances by each KIP's stored size and skips invalid layouts",
          "[core][loader_bounds]") {
    auto first = KipBytes({1}, 2, 1, 0, false, 11);
    auto invalid = KipBytes({2, 3}, 2, 2, 0, false, 22);
    Put(invalid, offsetof(FileSys::KIPHeader, sections), u32{0x1000});
    auto last = KipBytes({4, 5, 6}, 3, 3, 0, false, 33);
    FileSys::INIHeader header{};
    header.magic = Common::MakeMagic('I', 'N', 'I', '1');
    header.size = static_cast<u32>(sizeof(header) + first.size() + invalid.size() + last.size());
    header.kip_count = 3;
    std::vector<u8> bytes(sizeof(header));
    Put(bytes, 0, header);
    for (const auto& kip : {first, invalid, last}) {
        bytes.insert(bytes.end(), kip.begin(), kip.end());
    }
    const FileSys::INI ini{File(std::move(bytes))};
    REQUIRE(ini.GetStatus() == Loader::ResultStatus::Success);
    const auto& kips = ini.GetKIPs();
    REQUIRE(kips.size() == 2);
    CHECK(kips[0].GetTitleID() == 11);
    CHECK(kips[1].GetTitleID() == 33);
}

TEST_CASE("KIP loader maps only validated sections and zeroes unstored bytes",
          "[core][loader_bounds]") {
    WithProcess([](Kernel::KProcess& process, Core::System& system) {
        Loader::AppLoader_KIP oversized{File(KipBytes({0x72}, 4, 1, 0xF0000000, false))};
        CHECK(oversized.Load(process, system).first == Loader::ResultStatus::ErrorBadKIPHeader);
        Loader::AppLoader_KIP loader{File(KipBytes({0x72}, 4, 1, 0x1010, false))};
        REQUIRE(loader.Load(process, system).first == Loader::ResultStatus::Success);
        std::vector<u8> image(0x4000, 0x11);
        process.GetMemory().ReadBlock(process.GetEntryPoint(), image.data(), image.size());
        CHECK(image.front() == 0x72);
        CHECK(std::all_of(image.begin() + 1, image.end(), [](u8 byte) { return byte == 0; }));
    });
}
