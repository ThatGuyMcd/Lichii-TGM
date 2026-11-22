// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "common/fs/file.h"
#include "common/fs/path_util.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/hle/service/filesystem/filesystem.h"

namespace {

namespace fs = std::filesystem;

class TempDir {
public:
    TempDir() {
        const auto parent = fs::absolute(fs::temp_directory_path()).lexically_normal();
        for (unsigned attempt = 0; attempt < 64; ++attempt) {
            path = parent / ("tgm-vfs-lifetime-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(attempt));
            if (fs::create_directory(path)) {
                return;
            }
        }
        throw std::runtime_error("Cannot exclusively create VFS fixture directory");
    }
    ~TempDir() {
        const auto parent = fs::absolute(fs::temp_directory_path()).lexically_normal();
        if (path.parent_path() == parent &&
            path.filename().string().starts_with("tgm-vfs-lifetime-")) {
            std::error_code error;
            fs::remove_all(path, error);
        }
    }
    fs::path path;
};

fs::path MakeTempFile(const TempDir& temp, const std::string& name, const std::string& contents) {
    const fs::path path = temp.path / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
    return path;
}

} // Anonymous namespace

// Regression test for Lichii #1673: a RealVfsFile that outlives its
// RealVfsFilesystem must not dereference the destroyed filesystem. Before the
// fix this was a use-after-free (ASan) or an abort on hardened libc++ when
// ~RealVfsFile locked the destroyed filesystem's mutex during app shutdown.
TEST_CASE("RealVfsFile survives its RealVfsFilesystem", "[core][file_sys]") {
    const TempDir temp;
    const auto path = MakeTempFile(temp, "lichii_vfs_lifetime.txt", "hello-vfs");

    FileSys::VirtualFile file;
    FileSys::VirtualFile lazy;
    {
        FileSys::RealVfsFilesystem real_fs;
        file = real_fs.OpenFile(path.generic_string(), FileSys::OpenMode::Read);
        REQUIRE(file != nullptr);

        // Open the OS handle before teardown.
        char buf[16] = {};
        REQUIRE(file->Read(reinterpret_cast<u8*>(buf), sizeof(buf) - 1, 0) == 9);

        lazy = real_fs.OpenFile(path.generic_string(), FileSys::OpenMode::Read);
        REQUIRE(lazy != nullptr);
    } // real_fs is destroyed with both files outstanding

    // Destruction of the surviving files must be safe.
    file = nullptr;
    lazy = nullptr;
    SUCCEED("files outlived the filesystem without touching it");

    fs::remove(path);
}

TEST_CASE("RealVfsFile operations after filesystem teardown are defined", "[core][file_sys]") {
    const TempDir temp;
    const auto path = MakeTempFile(temp, "lichii_vfs_postmortem.txt", "hello-vfs");

    FileSys::VirtualFile file;
    {
        FileSys::RealVfsFilesystem real_fs;
        file = real_fs.OpenFile(path.generic_string(), FileSys::OpenMode::Read);
        REQUIRE(file != nullptr);
        char buf[16] = {};
        REQUIRE(file->Read(reinterpret_cast<u8*>(buf), sizeof(buf) - 1, 0) == 9);
    }

    // An already-open handle stays usable; the filesystem object is not touched.
    char buf[16] = {};
    CHECK(file->Read(reinterpret_cast<u8*>(buf), sizeof(buf) - 1, 0) == 9);

    // Operations that need the filesystem itself fail cleanly instead.
    CHECK(file->GetContainingDirectory() == nullptr);
    CHECK_FALSE(file->Rename("renamed"));

    file = nullptr;
    fs::remove(path);
}

TEST_CASE("RealVfsFilesystem alive during concurrent file destruction", "[core][file_sys]") {
    const TempDir temp;
    // Coverage: files are dropped on another thread while the filesystem is
    // alive, then the remaining files outlive the filesystem. This does NOT
    // race the filesystem's own destruction (see the next case for that).
    // Distinct paths: the cache dedupes same-path opens into one object.
    std::vector<fs::path> paths;
    for (int i = 0; i < 16; i++) {
        paths.push_back(MakeTempFile(temp, "lichii_vfs_concurrent_" + std::to_string(i) + ".txt",
                                     "hello-vfs"));
    }

    std::vector<FileSys::VirtualFile> files;
    {
        FileSys::RealVfsFilesystem real_fs;
        for (int round = 0; round < 4; round++) {
            for (const auto& p : paths) {
                files.push_back(real_fs.OpenFile(p.generic_string(), FileSys::OpenMode::Read));
            }
        }
        std::thread t([&] {
            for (int i = 0; i < 32; i++) {
                files[i] = nullptr;
            }
        });
        t.join();
    } // real_fs dies with files outstanding

    for (auto& f : files) {
        f = nullptr;
    }
    SUCCEED("concurrent destruction completed safely");

    for (const auto& p : paths) {
        fs::remove(p);
    }
}

TEST_CASE("RealVfsFilesystem destroyed concurrently with file destruction", "[core][file_sys]") {
    const TempDir temp;
    // Genuine concurrent teardown: the filesystem object dies on this thread
    // while worker threads are destroying files it handed out. Safe because
    // file destruction only touches the shared state block, which outlives
    // every participant.
    std::vector<fs::path> paths;
    for (int i = 0; i < 8; i++) {
        paths.push_back(MakeTempFile(temp, "lichii_vfs_race_" + std::to_string(i) + ".txt",
                                     "hello-vfs"));
    }

    auto real_fs = std::make_unique<FileSys::RealVfsFilesystem>();
    std::vector<FileSys::VirtualFile> files;
    for (const auto& p : paths) {
        files.push_back(real_fs->OpenFile(p.generic_string(), FileSys::OpenMode::Read));
        REQUIRE(files.back() != nullptr);
    }

    std::atomic<bool> go{false};
    auto dropper = [&](std::size_t begin, std::size_t end) {
        while (!go.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::size_t i = begin; i < end; i++) {
            files[i] = nullptr;
        }
    };
    std::thread t1(dropper, 0, files.size() / 2);
    std::thread t2(dropper, files.size() / 2, files.size());

    go.store(true, std::memory_order_release);
    real_fs.reset(); // destroy the filesystem while the droppers run
    t1.join();
    t2.join();

    SUCCEED("filesystem destruction raced file destruction safely");

    for (const auto& p : paths) {
        fs::remove(p);
    }
}

TEST_CASE("RealVfsFilesystem cache shares file objects for the same path", "[core][file_sys]") {
    const TempDir temp;
    const auto path = MakeTempFile(temp, "lichii_vfs_cache.txt", "hello-vfs");

    FileSys::RealVfsFilesystem real_fs;
    auto a = real_fs.OpenFile(path.generic_string(), FileSys::OpenMode::Read);
    auto b = real_fs.OpenFile(path.generic_string(), FileSys::OpenMode::Read);
    REQUIRE(a != nullptr);
    CHECK(a == b);

    fs::remove(path);
}

TEST_CASE("RealVfsFile does not open a lazy handle after filesystem teardown", "[core][file_sys]") {
    const TempDir temp;
    const auto path = MakeTempFile(temp, "lazy.txt", "hello-vfs");
    FileSys::VirtualFile file;
    {
        FileSys::RealVfsFilesystem owner;
        file = owner.OpenFile(path.generic_string(), FileSys::OpenMode::ReadWrite);
        REQUIRE(file);
    }
    std::array<u8, 9> bytes{};
    CHECK(file->Read(bytes.data(), bytes.size(), 0) == 0);
    CHECK(file->Write(bytes.data(), bytes.size(), 0) == 0);
    CHECK_FALSE(file->Resize(1));
    CHECK(file->GetSize() == 0);
    CHECK(file->GetContainingDirectory() == nullptr);
    file.reset();
}

TEST_CASE("Replacing a deleted VFS file keeps the two file identities separate",
          "[core][file_sys][vfs_mutation]") {
    const TempDir temp;
    const auto path = MakeTempFile(temp, "original.bin", "old");
    const auto host_path = Common::FS::PathToUTF8String(path);
    FileSys::RealVfsFilesystem owner;
    auto original = owner.OpenFile(host_path, FileSys::OpenMode::ReadWrite);
    REQUIRE(original);

    SECTION("already open") {
        REQUIRE(original->ReadByte(0) == 'o');
        REQUIRE(owner.DeleteFile(host_path));
        REQUIRE_FALSE(owner.OpenFile(host_path));
        const auto replacement = owner.CreateFile(host_path);
        REQUIRE(replacement);
        REQUIRE(replacement->WriteByte('n'));
#ifdef _WIN32
        CHECK_FALSE(original->ReadByte(0).has_value());
        CHECK_FALSE(original->WriteByte('x'));
#else
        CHECK(original->ReadByte(0) == 'o');
        REQUIRE(original->WriteByte('x'));
#endif
        CHECK_FALSE(original->Rename("other.bin"));
        CHECK(replacement->ReadByte(0) == 'n');
        original.reset();
        CHECK(owner.OpenFile(host_path) == replacement);
    }

    SECTION("lazy directory entry") {
        const auto directory = owner.OpenDirectory(Common::FS::PathToUTF8String(temp.path));
        original.reset();
        auto entries = directory->GetFiles();
        REQUIRE(entries.size() == 1);
        original = entries.front();
        REQUIRE(original->GetSize() == 3);
        REQUIRE(directory->DeleteFileWithResult("original.bin") == ResultSuccess);
        const auto replacement = owner.CreateFile(host_path);
        REQUIRE(replacement);
        REQUIRE(replacement->WriteByte('n'));
        CHECK(original->GetSize() == 0);
        CHECK_FALSE(original->ReadByte(0).has_value());
        CHECK(replacement->ReadByte(0) == 'n');
    }
}

TEST_CASE("A live VFS rename retains its cache entry and rolls back on failure",
          "[core][file_sys][vfs_mutation]") {
    const TempDir temp;
    const auto old_path = MakeTempFile(temp, "original.bin", "a");
    const auto new_path = MakeTempFile(temp, "destination.bin", "b");
    const auto old_host_path = Common::FS::PathToUTF8String(old_path);
    const auto new_host_path = Common::FS::PathToUTF8String(new_path);
    FileSys::RealVfsFilesystem owner;
    const auto original = owner.OpenFile(old_host_path);
    const auto destination = owner.OpenFile(new_host_path);
    REQUIRE(original);
    REQUIRE(destination);
    REQUIRE(original->ReadByte(0) == 'a');
    REQUIRE(destination->ReadByte(0) == 'b');

    CHECK_FALSE(original->Rename("destination.bin"));
    CHECK(owner.OpenFile(old_host_path) == original);
    CHECK(owner.OpenFile(new_host_path) == destination);
    CHECK(original->ReadByte(0) == 'a');
    CHECK(destination->ReadByte(0) == 'b');

    REQUIRE(owner.DeleteFile(new_host_path));
    REQUIRE(original->Rename("destination.bin"));
    CHECK_FALSE(owner.OpenFile(old_host_path));
    CHECK(owner.OpenFile(new_host_path) == original);
    CHECK(original->GetName() == "destination.bin");
    CHECK(Common::FS::SanitizePath(original->GetHostPath()) ==
          Common::FS::SanitizePath(new_host_path));
    CHECK(original->ReadByte(0) == 'a');
}

TEST_CASE("Creating over an open VFS file truncates it", "[core][file_sys][vfs_mutation]") {
    const TempDir temp;
    const auto path = MakeTempFile(temp, "original.bin", "abc");
    const auto host_path = Common::FS::PathToUTF8String(path);
    FileSys::RealVfsFilesystem owner;
    const auto original = owner.OpenFile(host_path);
    REQUIRE(original);
    REQUIRE(original->ReadByte(0) == 'a');

    const auto replacement = owner.CreateFile(host_path);
    REQUIRE(replacement);
    CHECK(replacement != original);
    CHECK(replacement->GetSize() == 0);
    REQUIRE(replacement->WriteByte('n'));
    CHECK(replacement->ReadByte(0) == 'n');
#ifdef _WIN32
    CHECK_FALSE(original->ReadByte(0).has_value());
#endif
}

#ifdef _WIN32
TEST_CASE("Guest file deletion distinguishes an independent Windows lock",
          "[core][file_sys][vfs_mutation]") {
    const TempDir temp;
    const auto path = MakeTempFile(temp, "original.bin", "abc");
    FileSys::RealVfsFilesystem owner;
    const auto dir = owner.OpenDirectory(Common::FS::PathToUTF8String(temp.path));
    const auto original = dir->GetFile("original.bin");
    REQUIRE(original);
    REQUIRE(original->ReadByte(0) == 'a');
    Common::FS::IOFile external{path, Common::FS::FileAccessMode::Read};
    REQUIRE(external.IsOpen());
    const Service::FileSystem::VfsDirectoryServiceWrapper guest{dir};

    CHECK(guest.DeleteFile("/original.bin") == FileSys::ResultTargetLocked);
    CHECK(fs::exists(path));
    CHECK(dir->GetFile("original.bin") == original);
    CHECK(original->ReadByte(1) == 'b');
    external.Close();
    CHECK(guest.DeleteFile("/original.bin") == ResultSuccess);
    CHECK_FALSE(fs::exists(path));
    CHECK_FALSE(original->ReadByte(0).has_value());
}
#endif
