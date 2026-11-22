// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif
#include "common/assert.h"
#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_real.h"

// For FileTimeStampRaw
#include <sys/stat.h>

#ifdef _WIN32
#include <cstdio> // _getmaxstdio
#else
#include <sys/resource.h> // getrlimit
#endif

#ifdef _MSC_VER
#define stat _stat64
#endif

#ifdef __ANDROID__
#include "common/fs/fs_android.h"
#endif

namespace FileSys {

namespace FS = Common::FS;

namespace {

// Files the cache keeps open. It has to stay below the process's open-file limit (the C runtime's
// on Windows, RLIMIT_NOFILE elsewhere) with room to spare: at the limit every other open fails
// (saves, shader caches, and the cache's own next file, which it only evicts for once it reaches
// its cap).
size_t MaxOpenFiles() {
#ifdef _WIN32
    static const size_t limit = static_cast<size_t>((std::max)(_getmaxstdio() - 512, 64));
#else
    static const size_t limit = [] {
        struct rlimit rl;
        if (getrlimit(RLIMIT_NOFILE, &rl) != 0 || rl.rlim_cur == RLIM_INFINITY) {
            return size_t{8192};
        }
        return static_cast<size_t>(std::max<rlim_t>(rl.rlim_cur, 576) - 512);
    }();
#endif
    return (std::min)(size_t{8192}, limit);
}

bool IsWithinRoot(std::string_view root, std::string_view full_path) {
    if (root.empty())
        return true;

    if (full_path.size() < root.size() || full_path.substr(0, root.size()) != root)
        return false;

    return full_path.size() == root.size() || full_path[root.size()] == '/' || full_path[root.size()] == '\\';
}

constexpr FS::FileAccessMode ModeFlagsToFileAccessMode(OpenMode mode) {
    switch (mode) {
    case OpenMode::Read:
        return FS::FileAccessMode::Read;
    case OpenMode::Write:
    case OpenMode::ReadWrite:
    case OpenMode::AllowAppend:
    case OpenMode::All:
        return FS::FileAccessMode::ReadWrite;
    default:
        return {};
    }
}

} // Anonymous namespace

RealVfsFilesystem::RealVfsFilesystem() : VfsFilesystem(nullptr) {}
RealVfsFilesystem::~RealVfsFilesystem() {
    // Files outliving this object only touch the shared state block, which
    // stays valid until the last file referencing it is gone. Mark the
    // filesystem as gone so outstanding files stop mutating the reference
    // lists and cache during teardown (see Lichii #1673).
    state->alive.store(false);
}

std::string RealVfsFilesystem::GetName() const {
    return "Real";
}

bool RealVfsFilesystem::IsReadable() const {
    return true;
}

bool RealVfsFilesystem::IsWritable() const {
    return true;
}

VfsEntryType RealVfsFilesystem::GetEntryType(std::string_view path_) const {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    if (!FS::Exists(path)) {
        return VfsEntryType::None;
    }
    if (FS::IsDir(path)) {
        return VfsEntryType::Directory;
    }

    return VfsEntryType::File;
}

VirtualFile RealVfsFilesystem::OpenFileFromEntry(std::string_view path_, std::optional<u64> size,
                                                 OpenMode perms) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    std::scoped_lock lk{state->list_lock};
    return OpenFileLocked(path, size, perms);
}

VirtualFile RealVfsFilesystem::OpenFileLocked(const std::string& path, std::optional<u64> size,
                                             OpenMode perms) {
    if (auto it = state->cache.find(path); it != state->cache.end()) {
        if (auto file = it->second.file.lock()) {
            return file;
        }
        CloseReferenceLocked(*state, *it->second.reference);
        DetachReferenceLocked(*state, it);
    }
    if (!size && !FS::IsFile(path)) {
        return nullptr;
    }

    auto reference = std::make_unique<FileReference>();
    reference->path = path;
    reference->size = size;
    const auto entry = state->cache.try_emplace(path).first;
    std::shared_ptr<RealVfsFile> file;
    try {
        file = std::make_shared<RealVfsFile>(*this, state, std::move(reference), perms);
    } catch (...) {
        state->cache.erase(entry);
        throw;
    }
    InsertReferenceIntoListLocked(*state, *file->reference);
    entry->second = {file, file->reference.get()};
    return file;
}

VirtualFile RealVfsFilesystem::OpenFile(std::string_view path_, OpenMode perms) {
    return OpenFileFromEntry(path_, {}, perms);
}

VirtualFile RealVfsFilesystem::CreateFile(std::string_view path_, OpenMode perms) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    std::scoped_lock lk{state->list_lock};
    const auto entry = state->cache.find(path);
#ifdef _WIN32
    if (entry != state->cache.end()) {
        CloseReferenceLocked(*state, *entry->second.reference);
    }
#endif

    // Current usages of CreateFile expect to delete the contents of an existing file.
    if (FS::IsFile(path)) {
        FS::IOFile temp{path, FS::FileAccessMode::Write, FS::FileType::BinaryFile};
        if (!temp.IsOpen()) {
            return nullptr;
        }
    } else if (!FS::NewFile(path)) {
        return nullptr;
    }

    if (entry != state->cache.end()) {
        entry->second.reference->size.reset();
        DetachReferenceLocked(*state, entry);
    }
    return OpenFileLocked(path, {}, perms);
}

VirtualFile RealVfsFilesystem::CopyFile(std::string_view old_path_, std::string_view new_path_) {
    // Unused
    return nullptr;
}

VirtualFile RealVfsFilesystem::MoveFile(std::string_view old_path_, std::string_view new_path_) {
    const auto old_path = FS::SanitizePath(old_path_, FS::DirectorySeparator::PlatformDefault);
    const auto new_path = FS::SanitizePath(new_path_, FS::DirectorySeparator::PlatformDefault);
    std::scoped_lock lk{state->list_lock};
    if (!RenameFileLocked(*state, old_path, new_path)) {
        return nullptr;
    }
    return OpenFileLocked(new_path, {}, OpenMode::ReadWrite);
}

bool RealVfsFilesystem::RenameFileLocked(RealVfsSharedState& state, const std::string& old_path,
                                        std::string new_path) {
    if (old_path == new_path) {
        return false;
    }
    const auto source = state.cache.find(old_path);
    auto new_key = source == state.cache.end() ? std::string{} : new_path;
#ifdef _WIN32
    if (source != state.cache.end()) {
        CloseReferenceLocked(state, *source->second.reference);
    }
#endif
    if (!FS::RenameFile(old_path, new_path)) {
        return false;
    }

    if (auto destination = state.cache.find(new_path); destination != state.cache.end()) {
        DetachReferenceLocked(state, destination);
    }
    if (source != state.cache.end()) {
#ifndef _WIN32
        // The stream's size lookup uses the path it was opened with.
        CloseReferenceLocked(state, *source->second.reference);
#endif
        auto entry = state.cache.extract(source);
        entry.key() = std::move(new_key);
        entry.mapped().reference->path = std::move(new_path);
        state.cache.insert(std::move(entry));
    }
    return true;
}

bool RealVfsFilesystem::DeleteFile(std::string_view path_) {
    return DeleteFileWithResult(path_).IsSuccess();
}

Result RealVfsFilesystem::DeleteFileWithResult(std::string_view path_) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    std::scoped_lock lk{state->list_lock};
    const auto entry = state->cache.find(path);
#ifdef _WIN32
    if (entry != state->cache.end()) {
        CloseReferenceLocked(*state, *entry->second.reference);
    }
#endif
    std::error_code ec;
    if (!FS::RemoveFile(std::filesystem::path{FS::ToU8String(path)}, ec)) {
#ifdef _WIN32
        if (ec == std::error_code{ERROR_SHARING_VIOLATION, std::system_category()} ||
            ec == std::error_code{ERROR_LOCK_VIOLATION, std::system_category()}) {
            return ResultTargetLocked;
        }
#endif
        if (ec == std::errc::permission_denied || ec == std::errc::read_only_file_system) {
            return ResultPermissionDenied;
        }
        if (ec == std::errc::invalid_argument || ec == std::errc::is_a_directory) {
            return ResultInvalidPath;
        }
        return ResultUnknown;
    }
    if (entry != state->cache.end()) {
        DetachReferenceLocked(*state, entry);
    }
    return ResultSuccess;
}

VirtualDir RealVfsFilesystem::OpenDirectory(std::string_view path_, OpenMode perms) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    return std::make_shared<RealVfsDirectory>(*this, path, perms);
}

VirtualDir RealVfsFilesystem::CreateDirectory(std::string_view path_, OpenMode perms) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    if (!FS::CreateDirs(path)) {
        return nullptr;
    }
    return std::make_shared<RealVfsDirectory>(*this, path, perms);
}

VirtualDir RealVfsFilesystem::CopyDirectory(std::string_view old_path_,
                                            std::string_view new_path_) {
    // Unused
    return nullptr;
}

VirtualDir RealVfsFilesystem::MoveDirectory(std::string_view old_path_,
                                            std::string_view new_path_) {
    const auto old_path = FS::SanitizePath(old_path_, FS::DirectorySeparator::PlatformDefault);
    const auto new_path = FS::SanitizePath(new_path_, FS::DirectorySeparator::PlatformDefault);

    if (!FS::RenameDir(old_path, new_path)) {
        return nullptr;
    }
    return OpenDirectory(new_path, OpenMode::ReadWrite);
}

bool RealVfsFilesystem::DeleteDirectory(std::string_view path_) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    return FS::RemoveDirRecursively(path);
}

void RealVfsFilesystem::RefreshReferenceLocked(RealVfsSharedState& state,
                                               FileReference& reference, OpenMode perms) {
    // Outstanding files retain open handles after the filesystem goes away.
    if (!state.alive.load()) {
        return;
    }

    if (reference.file) {
        state.open_references.erase(state.open_references.iterator_to(reference));
        state.open_references.push_front(reference);
        return;
    }
    if (reference.detached) {
        return;
    }
    EvictSingleReferenceLocked(state);
    auto file = FS::FileOpen(reference.path, ModeFlagsToFileAccessMode(perms),
                             FS::FileType::BinaryFile);
    if (file) {
        RemoveReferenceFromListLocked(state, reference);
        reference.file = std::move(file);
        ++state.num_open_files;
        InsertReferenceIntoListLocked(state, reference);
    }
}

void RealVfsFilesystem::DropReference(RealVfsSharedState& state, FileReference& reference) {
    std::scoped_lock lk{state.list_lock};
    RemoveReferenceFromListLocked(state, reference);
    if (const auto it = state.cache.find(reference.path);
        it != state.cache.end() && it->second.reference == &reference) {
        state.cache.erase(it);
    }
    if (reference.file) {
        reference.file.reset();
        --state.num_open_files;
    }
}

void RealVfsFilesystem::DetachReferenceLocked(RealVfsSharedState& state,
                                              RealVfsSharedState::FileCache::iterator entry) {
    entry->second.reference->detached = true;
    state.cache.erase(entry);
}

void RealVfsFilesystem::CloseReferenceLocked(RealVfsSharedState& state, FileReference& reference) {
    if (!reference.file) {
        return;
    }
    RemoveReferenceFromListLocked(state, reference);
    reference.file.reset();
    --state.num_open_files;
    InsertReferenceIntoListLocked(state, reference);
}

void RealVfsFilesystem::EvictSingleReferenceLocked(RealVfsSharedState& state) {
    if (state.num_open_files >= MaxOpenFiles() && !state.open_references.empty()) {
        CloseReferenceLocked(state, state.open_references.back());
    }
}

void RealVfsFilesystem::InsertReferenceIntoListLocked(RealVfsSharedState& state,
                                                      FileReference& reference) {
    if (reference.file) {
        state.open_references.push_front(reference);
    } else {
        state.closed_references.push_front(reference);
    }
}

void RealVfsFilesystem::RemoveReferenceFromListLocked(RealVfsSharedState& state,
                                                      FileReference& reference) {
    if (reference.file) {
        state.open_references.erase(state.open_references.iterator_to(reference));
    } else {
        state.closed_references.erase(state.closed_references.iterator_to(reference));
    }
}

RealVfsFile::RealVfsFile(RealVfsFilesystem& base_, std::shared_ptr<RealVfsSharedState> state_,
                         std::unique_ptr<FileReference> reference_, OpenMode perms_)
    : base(base_), state(std::move(state_)), reference(std::move(reference_)), perms(perms_) {}

RealVfsFile::~RealVfsFile() {
    RealVfsFilesystem::DropReference(*state, *reference);
}

std::string RealVfsFile::GetName() const {
    std::scoped_lock lk{state->list_lock};
    const auto& path = reference->path;
#ifdef __ANDROID__
    if (!path.empty() && path[0] != '/') {
        return FS::Android::GetFilename(path);
    }
#endif
    // The last path component.
    const auto end = path.find_last_not_of("\\/");
    if (end == std::string::npos) {
        return "";
    }
    const auto separator = path.find_last_of("\\/", end);
    const auto begin = separator == std::string::npos ? 0 : separator + 1;
    return path.substr(begin, end + 1 - begin);
}

std::string RealVfsFile::GetHostPath() const {
    std::scoped_lock lk{state->list_lock};
    return reference->path;
}

std::size_t RealVfsFile::GetSize() const {
    std::scoped_lock lk{state->list_lock};
    if (reference->size && !reference->detached) {
        return *reference->size;
    }
    RealVfsFilesystem::RefreshReferenceLocked(*state, *reference, perms);
    return reference->file ? reference->file->GetSize() : 0;
}

bool RealVfsFile::Resize(std::size_t new_size) {
    std::scoped_lock lk{state->list_lock};
    reference->size.reset();
    RealVfsFilesystem::RefreshReferenceLocked(*state, *reference, perms);
    return reference->file ? reference->file->SetSize(new_size) : false;
}

VirtualDir RealVfsFile::GetContainingDirectory() const {
    // The filesystem must outlive any active call that hands out one of its directories.
    if (!state->alive.load()) {
        return nullptr;
    }
    return base.OpenDirectory(FS::GetParentPath(GetHostPath()), perms);
}

bool RealVfsFile::IsWritable() const {
    return True(perms & OpenMode::Write);
}

bool RealVfsFile::IsReadable() const {
    return True(perms & OpenMode::Read);
}

std::size_t RealVfsFile::Read(u8* data, std::size_t length, std::size_t offset) const {
    std::scoped_lock lk{state->list_lock};
    RealVfsFilesystem::RefreshReferenceLocked(*state, *reference, perms);
    if (!reference->file || !reference->file->Seek(static_cast<s64>(offset))) {
        return 0;
    }
    return reference->file->ReadSpan(std::span{data, length});
}

std::size_t RealVfsFile::Write(const u8* data, std::size_t length, std::size_t offset) {
    std::scoped_lock lk{state->list_lock};
    reference->size.reset();
    RealVfsFilesystem::RefreshReferenceLocked(*state, *reference, perms);
    if (!reference->file || !reference->file->Seek(static_cast<s64>(offset))) {
        return 0;
    }
    return reference->file->WriteSpan(std::span{data, length});
}

bool RealVfsFile::Rename(std::string_view name) {
    std::scoped_lock lk{state->list_lock};
    if (!state->alive.load() || reference->detached) {
        return false;
    }
    const auto& path = reference->path;
    auto new_path = FS::SanitizePath(FS::GetParentPath(path) + '/' + std::string(name),
                                     FS::DirectorySeparator::PlatformDefault);
    return RealVfsFilesystem::RenameFileLocked(*state, path, std::move(new_path));
}

// TODO(DarkLordZach): MSVC would not let me combine the following two functions using 'if
// constexpr' because there is a compile error in the branch not used.

template <>
std::vector<VirtualFile> RealVfsDirectory::IterateEntries<RealVfsFile, VfsFile>() const {
    if (perms == OpenMode::AllowAppend) {
        return {};
    }

    std::vector<VirtualFile> out;

    const FS::DirEntryCallable callback = [this,
                                           &out](const std::filesystem::directory_entry& entry) {
        const auto full_path_string = FS::PathToUTF8String(entry.path());

        out.emplace_back(base.OpenFileFromEntry(full_path_string, entry.file_size(), perms));

        return true;
    };

    FS::IterateDirEntries(path, callback, FS::DirEntryFilter::File);

    return out;
}

template <>
std::vector<VirtualDir> RealVfsDirectory::IterateEntries<RealVfsDirectory, VfsDirectory>() const {
    if (perms == OpenMode::AllowAppend) {
        return {};
    }

    std::vector<VirtualDir> out;

    const FS::DirEntryCallable callback = [this,
                                           &out](const std::filesystem::directory_entry& entry) {
        const auto full_path_string = FS::PathToUTF8String(entry.path());

        out.emplace_back(base.OpenDirectory(full_path_string, perms));

        return true;
    };

    FS::IterateDirEntries(path, callback, FS::DirEntryFilter::Directory);

    return out;
}

RealVfsDirectory::RealVfsDirectory(RealVfsFilesystem& base_, const std::string& path_,
                                   OpenMode perms_)
    : base(base_), path(FS::RemoveTrailingSlash(path_)), parent_path(FS::GetParentPath(path)),
      path_components(FS::SplitPathComponentsCopy(path)), perms(perms_) {
    if (!FS::Exists(path) && True(perms & OpenMode::Write)) {
        void(FS::CreateDirs(path));
    }
}

RealVfsDirectory::~RealVfsDirectory() = default;

VirtualFile RealVfsDirectory::GetFileRelative(std::string_view relative_path) const {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(relative_path));
    if (!FS::Exists(full_path) || FS::IsDir(full_path)
        || !IsWithinRoot(FS::SanitizePath(path), full_path)) {
        return nullptr;
    }
    return base.OpenFile(full_path, perms);
}

VirtualDir RealVfsDirectory::GetDirectoryRelative(std::string_view relative_path) const {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(relative_path));
    if (!FS::Exists(full_path) || !FS::IsDir(full_path)
        || !IsWithinRoot(FS::SanitizePath(path), full_path)) {
        return nullptr;
    }
    return base.OpenDirectory(full_path, perms);
}

VirtualFile RealVfsDirectory::GetFile(std::string_view name) const {
    return GetFileRelative(name);
}

VirtualDir RealVfsDirectory::GetSubdirectory(std::string_view name) const {
    return GetDirectoryRelative(name);
}

VirtualFile RealVfsDirectory::CreateFileRelative(std::string_view relative_path) {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(relative_path));
    if (!FS::CreateParentDirs(full_path) || !IsWithinRoot(FS::SanitizePath(path), full_path)) {
        return nullptr;
    }
    return base.CreateFile(full_path, perms);
}

VirtualDir RealVfsDirectory::CreateDirectoryRelative(std::string_view relative_path) {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(relative_path));
    return base.CreateDirectory(full_path, perms);
}

bool RealVfsDirectory::DeleteSubdirectoryRecursive(std::string_view name) {
    const auto full_path = FS::SanitizePath(this->path + '/' + std::string(name));
    return FS::RemoveDirRecursively(full_path);
}

std::vector<VirtualFile> RealVfsDirectory::GetFiles() const {
    return IterateEntries<RealVfsFile, VfsFile>();
}

FileTimeStampRaw RealVfsDirectory::GetFileTimeStamp(std::string_view path_) const {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(path_));
    const auto fs_path = std::filesystem::path{FS::ToU8String(full_path)};

#ifdef _WIN32
    struct _stat64 file_status;
    const auto stat_result = _wstat64(fs_path.c_str(), &file_status);
#else
    struct stat file_status;
    const auto stat_result = stat(fs_path.c_str(), &file_status);
#endif

    if (stat_result != 0) {
        return {};
    }

    return {
        .created{static_cast<u64>(file_status.st_ctime)},
        .accessed{static_cast<u64>(file_status.st_atime)},
        .modified{static_cast<u64>(file_status.st_mtime)},
    };
}

std::vector<VirtualDir> RealVfsDirectory::GetSubdirectories() const {
    return IterateEntries<RealVfsDirectory, VfsDirectory>();
}

bool RealVfsDirectory::IsWritable() const {
    return True(perms & OpenMode::Write);
}

bool RealVfsDirectory::IsReadable() const {
    return True(perms & OpenMode::Read);
}

std::string RealVfsDirectory::GetName() const {
    return path_components.empty() ? "" : std::string(path_components.back());
}

VirtualDir RealVfsDirectory::GetParentDirectory() const {
    if (path_components.size() <= 1) {
        return nullptr;
    }

    return base.OpenDirectory(parent_path, perms);
}

VirtualDir RealVfsDirectory::CreateSubdirectory(std::string_view name) {
    const std::string subdir_path = (path + '/').append(name);
    return base.CreateDirectory(subdir_path, perms);
}

VirtualFile RealVfsDirectory::CreateFile(std::string_view name) {
    const std::string file_path = (path + '/').append(name);
    return base.CreateFile(file_path, perms);
}

bool RealVfsDirectory::DeleteSubdirectory(std::string_view name) {
    const std::string subdir_path = (path + '/').append(name);
    return FS::RemoveDir(subdir_path);
}

bool RealVfsDirectory::DeleteFile(std::string_view name) {
    return DeleteFileWithResult(name).IsSuccess();
}

Result RealVfsDirectory::DeleteFileWithResult(std::string_view name) {
    const std::string file_path = (path + '/').append(name);
    return base.DeleteFileWithResult(file_path);
}

bool RealVfsDirectory::Rename(std::string_view name) {
    const std::string new_name = (parent_path + '/').append(name);
    return base.MoveFile(path, new_name) != nullptr;
}

std::string RealVfsDirectory::GetFullPath() const {
    auto out = path;
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

std::map<std::string, VfsEntryType, std::less<>> RealVfsDirectory::GetEntries() const {
    if (perms == OpenMode::AllowAppend) {
        return {};
    }

    std::map<std::string, VfsEntryType, std::less<>> out;

    const FS::DirEntryCallable callback = [&out](const std::filesystem::directory_entry& entry) {
        const auto filename = FS::PathToUTF8String(entry.path().filename());
        out.insert_or_assign(filename,
                             entry.is_directory() ? VfsEntryType::Directory : VfsEntryType::File);
        return true;
    };

    FS::IterateDirEntries(path, callback);

    return out;
}

} // namespace FileSys
