// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

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
    run([&](FileSys::VirtualFile file, bool arguments = false) {
        return Loader::AppLoader_NSO::LoadModule(process, system, *file, 0, arguments, false);
    });
}

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

FileSys::VirtualFile Kip(std::vector<u8> payload, u32 decompressed = 32,
                        u32 declared = 28, u32 bss = 0) {
    FileSys::KIPHeader header{};
    header.magic = Common::MakeMagic('K', 'I', 'P', '1');
    header.sections[0].decompressed_size = decompressed;
    header.sections[0].compressed_size = declared;
    header.sections[3].decompressed_size = bss;
    std::vector<u8> bytes(sizeof(header));
    Put(bytes, 0, header);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return File(std::move(bytes));
}
} // namespace

TEST_CASE("Upstream NSO bounds reject corrupt images before loading", "[core][loader_bounds]") {
    WithProcess([](auto load) {
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
    const FileSys::KIP valid{Kip(Blz())};
    REQUIRE(valid.GetStatus() == Loader::ResultStatus::Success);
    const auto text = valid.GetSectionDecompressed(0);
    CHECK(std::string_view{reinterpret_cast<const char*>(text.data()), text.size()} ==
          "ABCDEFGHABCDEFGHABCDEFGHABCDEFGH");
    for (const u32 size : {29U, 0xFFFFFFFFU}) {
        const FileSys::KIP truncated{Kip(Blz(), 32, size)};
        CHECK(truncated.GetStatus() == Loader::ResultStatus::ErrorBadKIPHeader);
    }
    const FileSys::KIP stored{Kip(std::vector<u8>(16, 0x66), 16, 16, 0xFFFFFFF0)};
    REQUIRE(stored.GetStatus() == Loader::ResultStatus::Success);
    CHECK(stored.GetTextSection().size() == 16);
    CHECK(stored.GetBSSSize() == 0xFFFFFFF0);
    CHECK(stored.GetSectionDecompressed(3).empty());
    for (const auto [offset, value] : {std::pair{16U, 0x1000U}, std::pair{20U, 29U},
                                     std::pair{24U, 0xFFFFFFF0U}, std::pair{24U, 0xFFFFFFE4U}}) {
        auto corrupt = Blz();
        Put(corrupt, offset, value);
        const FileSys::KIP bad{Kip(std::move(corrupt))};
        CHECK(bad.GetStatus() == Loader::ResultStatus::ErrorBLZDecompressionFailed);
    }
    auto exhausted = Blz();
    exhausted[6] = 0;
    const FileSys::KIP underflow{Kip(std::move(exhausted))};
    CHECK(underflow.GetStatus() == Loader::ResultStatus::ErrorBLZDecompressionFailed);
}
