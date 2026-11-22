// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <memory>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/core.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "core/hle/service/filesystem/fsp/fs_i_file.h"
#include "core/hle/service/filesystem/fsp/fs_i_storage.h"
#include "tests/core/system_test_context.h"

namespace Service::FileSystem {
struct FileIoBoundsTestAccess {
    static constexpr int Attributes = BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure;

    static Result Read(IFile& file, s64 offset, s64 length, std::span<u8> output, s64& read) {
        return file.Read(FileSys::ReadOption::None, &read, offset, OutBuffer<Attributes>{output}, length);
    }
    static Result Read(IStorage& storage, s64 offset, s64 length, std::span<u8> output) {
        return storage.Read(OutBuffer<Attributes>{output}, offset, length);
    }
    static Result Write(IFile& file, s64 offset, s64 length, std::span<const u8> input) {
        return file.Write(InBuffer<Attributes>{input}, FileSys::WriteOption::None, offset, length);
    }
};
} // namespace Service::FileSystem

TEST_CASE("File services validate the requested length against the mapped span",
          "[core][filesystem_buffer_bounds]") {
    ScopedSystemTestContext context;
    Core::System system;
    const std::vector<u8> original{10, 20, 30, 40, 50, 60};
    auto data = std::make_shared<FileSys::VectorVfsFile>(original);
    Service::FileSystem::IFile file{system, data};
    Service::FileSystem::IStorage storage{system, data};
    using Access = Service::FileSystem::FileIoBoundsTestAccess;
    const std::array<u8, 6> sentinels{0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5};
    auto output = sentinels;
    auto buffer = std::span{output}.subspan(1, 3);
    s64 read = -1;

    SECTION("Invalid requests leave the buffer and backing data untouched") {
        for (const s64 size : {-1, 4}) {
            CHECK(Access::Read(file, 0, size, buffer, read) == FileSys::ResultInvalidSize);
            CHECK(Access::Read(storage, 0, size, buffer) == FileSys::ResultInvalidSize);
            CHECK(Access::Write(file, 0, size, buffer) == FileSys::ResultInvalidSize);
        }
        CHECK(read == -1);
        CHECK(output == sentinels);
        CHECK(data->ReadAllBytes() == original);
    }
    SECTION("An exact-size read and write respect their offsets") {
        REQUIRE(Access::Read(file, 2, 3, buffer, read) == ResultSuccess);
        CHECK(read == 3);
        CHECK(output == std::array<u8, 6>{0xA5, 30, 40, 50, 0xA5, 0xA5});
        output = sentinels;
        REQUIRE(Access::Read(storage, 1, 3, buffer) == ResultSuccess);
        CHECK(output == std::array<u8, 6>{0xA5, 20, 30, 40, 0xA5, 0xA5});
        REQUIRE(Access::Write(file, 3, 3, buffer) == ResultSuccess);
        CHECK(data->ReadAllBytes() == std::vector<u8>{10, 20, 30, 20, 30, 40});
    }
    SECTION("Valid empty requests remain supported") {
        CHECK(Access::Read(file, 0, 0, {}, read) == ResultSuccess);
        CHECK(read == 0);
        CHECK(Access::Read(storage, 0, 0, {}) == ResultSuccess);
        CHECK(Access::Write(file, 0, 0, {}) == ResultSuccess);
        CHECK(data->ReadAllBytes() == original);
    }
}
