#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace cppfm::storage_file_ops {

// A write handle for a new path that must never follow or replace an
// existing entry. Recovery code uses this for temporary siblings because a
// prior symlink (including a dangling one) must not redirect writes outside
// the world directory.
class ExclusiveFileWriter final {
public:
    ExclusiveFileWriter() = default;
    ~ExclusiveFileWriter() { closeNoThrow(); }
    ExclusiveFileWriter(const ExclusiveFileWriter&) = delete;
    ExclusiveFileWriter& operator=(const ExclusiveFileWriter&) = delete;

    bool createNew(const std::filesystem::path& path,
                   std::error_code& error,
                   unsigned int posixMode = 0600) {
        if (isOpen()) {
            error = std::make_error_code(std::errc::device_or_resource_busy);
            return false;
        }
#ifdef _WIN32
        (void)posixMode;
        const HANDLE handle = CreateFileW(
            path.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const auto nativeError = GetLastError();
            if (nativeError == ERROR_ALREADY_EXISTS ||
                nativeError == ERROR_FILE_EXISTS)
                error = std::make_error_code(std::errc::file_exists);
            else
                error = std::error_code(static_cast<int>(nativeError),
                                        std::system_category());
            return false;
        }
        handle_ = handle;
#elif defined(__unix__)
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
        fd_ = ::open(path.c_str(), flags, posixMode);
        if (fd_ < 0) {
            error = std::error_code(errno, std::generic_category());
            return false;
        }
#else
        (void)path;
        (void)posixMode;
        error = std::make_error_code(std::errc::operation_not_supported);
        return false;
#endif
        error.clear();
        return true;
    }

    bool write(const void* bytes, std::size_t size,
               std::error_code& error) noexcept {
        if (!isOpen()) {
            error = std::make_error_code(std::errc::bad_file_descriptor);
            return false;
        }
        const auto* data = static_cast<const char*>(bytes);
        std::size_t offset = 0;
        while (offset < size) {
#ifdef _WIN32
            const auto count = static_cast<DWORD>(std::min<std::size_t>(
                size - offset, std::numeric_limits<DWORD>::max()));
            DWORD written = 0;
            if (!WriteFile(handle_, data + offset, count, &written, nullptr)) {
                error = std::error_code(static_cast<int>(GetLastError()),
                                        std::system_category());
                return false;
            }
            if (written == 0) {
                error = std::make_error_code(std::errc::io_error);
                return false;
            }
            offset += written;
#elif defined(__unix__)
            const auto count = std::min<std::size_t>(
                size - offset,
                static_cast<std::size_t>(
                    std::numeric_limits<ssize_t>::max()));
            const auto written = ::write(fd_, data + offset, count);
            if (written < 0) {
                if (errno == EINTR) continue;
                error = std::error_code(errno, std::generic_category());
                return false;
            }
            if (written == 0) {
                error = std::make_error_code(std::errc::io_error);
                return false;
            }
            offset += static_cast<std::size_t>(written);
#else
            (void)data;
            (void)offset;
            error = std::make_error_code(std::errc::operation_not_supported);
            return false;
#endif
        }
        error.clear();
        return true;
    }

    bool syncAndClose(std::error_code& error) noexcept {
        if (!isOpen()) {
            error = std::make_error_code(std::errc::bad_file_descriptor);
            return false;
        }
        bool synced = false;
#ifdef _WIN32
        synced = FlushFileBuffers(handle_) != FALSE;
        if (!synced)
            error = std::error_code(static_cast<int>(GetLastError()),
                                    std::system_category());
        const HANDLE handle = std::exchange(handle_, INVALID_HANDLE_VALUE);
        const bool closed = CloseHandle(handle) != FALSE;
        if (synced && !closed)
            error = std::error_code(static_cast<int>(GetLastError()),
                                    std::system_category());
#elif defined(__unix__)
        int result = 0;
        do {
            result = ::fsync(fd_);
        } while (result != 0 && errno == EINTR);
        synced = result == 0;
        if (!synced)
            error = std::error_code(errno, std::generic_category());
        const int fd = std::exchange(fd_, -1);
        const bool closed = ::close(fd) == 0;
        if (synced && !closed)
            error = std::error_code(errno, std::generic_category());
#endif
        if (!synced) return false;
#ifdef _WIN32
        if (!closed) return false;
#elif defined(__unix__)
        if (!closed) return false;
#endif
        error.clear();
        return true;
    }

private:
    bool isOpen() const noexcept {
#ifdef _WIN32
        return handle_ != INVALID_HANDLE_VALUE;
#elif defined(__unix__)
        return fd_ >= 0;
#else
        return false;
#endif
    }

    void closeNoThrow() noexcept {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#elif defined(__unix__)
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }

#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#elif defined(__unix__)
    int fd_ = -1;
#endif
};

// Create one exact path and write it without following or replacing any
// existing directory entry. On failure, remove only the partial file this
// call actually created. createdOut remains true only when cleanup itself
// failed and the caller may still own a partial file at path.
inline bool writeFileExclusively(const std::filesystem::path& path,
                                 const void* bytes, std::size_t size,
                                 std::error_code& error,
                                 bool* createdOut = nullptr,
                                 unsigned int posixMode = 0666) {
    if (createdOut) *createdOut = false;
    bool created = false;
    bool written = false;
    {
        ExclusiveFileWriter writer;
        if (!writer.createNew(path, error, posixMode)) return false;
        created = true;
        if (createdOut) *createdOut = true;
        written = writer.write(bytes, size, error) &&
                  writer.syncAndClose(error);
    }
    if (written) {
        error.clear();
        return true;
    }
    if (created) {
        std::error_code cleanupError;
        std::filesystem::remove(path, cleanupError);
        if (!cleanupError && createdOut) *createdOut = false;
    }
    return false;
}

inline bool writeFileToNewSibling(const std::filesystem::path& base,
                                  const void* bytes, std::size_t size,
                                  std::filesystem::path& createdPath,
                                  std::error_code& error,
                                  bool* createdOut = nullptr,
                                  unsigned int posixMode = 0666) {
    if (createdOut) *createdOut = false;
    for (std::size_t suffix = 0; suffix < 10000; ++suffix) {
        auto candidate = base;
        if (suffix != 0) candidate += "." + std::to_string(suffix);
        createdPath = candidate;
        bool candidateCreated = false;
        if (writeFileExclusively(candidate, bytes, size, error,
                                 &candidateCreated, posixMode)) {
            if (createdOut) *createdOut = true;
            return true;
        }
        if (createdOut) *createdOut = candidateCreated;
        if (error != std::errc::file_exists) return false;
    }
    error = std::make_error_code(std::errc::file_exists);
    return false;
}

inline bool copyFileExclusively(const std::filesystem::path& source,
                                const std::filesystem::path& destination,
                                std::error_code& error,
                                bool* createdOut = nullptr,
                                unsigned int posixMode = 0666) {
    if (createdOut) *createdOut = false;
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }
    bool created = false;
    bool copied = false;
    {
        ExclusiveFileWriter writer;
        if (!writer.createNew(destination, error, posixMode)) return false;
        created = true;
        if (createdOut) *createdOut = true;
        std::array<char, 64 * 1024> buffer{};
        bool writeOk = true;
        while (input) {
            input.read(buffer.data(),
                       static_cast<std::streamsize>(buffer.size()));
            const auto bytesRead = input.gcount();
            if (bytesRead > 0 &&
                !writer.write(buffer.data(),
                              static_cast<std::size_t>(bytesRead), error)) {
                writeOk = false;
                break;
            }
        }
        if (writeOk && input.bad()) {
            error = std::make_error_code(std::errc::io_error);
            writeOk = false;
        }
        copied = writeOk && writer.syncAndClose(error);
    }
    if (copied) {
        error.clear();
        return true;
    }
    if (created) {
        std::error_code cleanupError;
        std::filesystem::remove(destination, cleanupError);
        if (!cleanupError && createdOut) *createdOut = false;
    }
    return false;
}

inline bool copyFileToNewSibling(const std::filesystem::path& source,
                                 const std::filesystem::path& base,
                                 std::filesystem::path& createdPath,
                                 std::error_code& error,
                                 bool* createdOut = nullptr,
                                 unsigned int posixMode = 0666) {
    if (createdOut) *createdOut = false;
    for (std::size_t suffix = 0; suffix < 10000; ++suffix) {
        auto candidate = base;
        if (suffix != 0) candidate += "." + std::to_string(suffix);
        createdPath = candidate;
        bool candidateCreated = false;
        if (copyFileExclusively(source, candidate, error,
                                &candidateCreated, posixMode)) {
            if (createdOut) *createdOut = true;
            return true;
        }
        if (createdOut) *createdOut = candidateCreated;
        if (error != std::errc::file_exists) return false;
    }
    error = std::make_error_code(std::errc::file_exists);
    return false;
}

// A successful stream flush only hands bytes to the kernel. Sync both the
// temporary file and its containing directory before treating replacement as
// durable.
inline bool syncFile(const std::filesystem::path& path) {
#ifdef _WIN32
    const HANDLE handle = CreateFileW(
        path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const bool ok = FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    return ok;
#elif defined(__unix__)
    // Some Unix implementations require a write-capable descriptor for
    // fsync on regular files, so do not rely on O_RDONLY being accepted.
    const int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) return false;
    int result = 0;
    do {
        result = ::fsync(fd);
    } while (result != 0 && errno == EINTR);
    const int closeResult = ::close(fd);
    return result == 0 && closeResult == 0;
#else
    (void)path;
    return true;
#endif
}

inline bool syncDirectory(const std::filesystem::path& path) {
#ifdef _WIN32
    (void)path;
    // Win32 has no directory-fsync path here. File replacement uses
    // MOVEFILE_WRITE_THROUGH, but syncing a newly created directory entry is
    // best-effort on this platform rather than a POSIX-style fsync guarantee.
    return true;
#elif defined(__unix__)
    const auto directory = path.empty() ? std::filesystem::path(".") : path;
    const int fd = ::open(directory.c_str(), O_RDONLY);
    if (fd < 0) return false;
    int result = 0;
    do {
        result = ::fsync(fd);
    } while (result != 0 && errno == EINTR);
    const int closeResult = ::close(fd);
    return result == 0 && closeResult == 0;
#else
    (void)path;
    return true;
#endif
}

// Persist each newly created directory entry where the platform supports
// directory syncing. Windows does not expose the same directory-fsync
// contract; its atomic file replacement uses MOVEFILE_WRITE_THROUGH instead.
inline bool createDirectoriesDurably(const std::filesystem::path& path,
                                     std::error_code& error) {
    if (path.empty()) {
        error.clear();
        return true;
    }
    std::vector<std::filesystem::path> missing;
    auto current = path;
    while (!current.empty()) {
        const bool exists = std::filesystem::exists(current, error);
        if (error) return false;
        if (exists) break;
        missing.push_back(current);
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }

    std::filesystem::create_directories(path, error);
    if (error) return false;

    // Sync from the highest newly created directory down so each ancestor's
    // name is durable before relying on entries nested inside it.
    for (auto directory = missing.rbegin(); directory != missing.rend();
         ++directory) {
        if (!syncDirectory(directory->parent_path())) {
            error = std::make_error_code(std::errc::io_error);
            return false;
        }
    }
    error.clear();
    return true;
}

// Replace an existing destination atomically on both supported OS families.
inline bool replaceFile(const std::filesystem::path& source,
                        const std::filesystem::path& destination,
                        std::error_code& error) {
#ifdef _WIN32
    if (MoveFileExW(source.wstring().c_str(), destination.wstring().c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error.clear();
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()),
                            std::system_category());
    return false;
#else
    std::filesystem::rename(source, destination, error);
    return !error;
#endif
}

// Move a file only when the destination name is unused. Quarantine and
// recovery code must never replace an older evidence file. On Unix, creating
// a hard link claims the destination atomically; on Windows, omitting
// MOVEFILE_REPLACE_EXISTING gives the same no-overwrite contract.
inline bool moveFileWithoutReplace(const std::filesystem::path& source,
                                   const std::filesystem::path& destination,
                                   std::error_code& error) {
#ifdef _WIN32
    if (MoveFileExW(source.wstring().c_str(), destination.wstring().c_str(),
                    MOVEFILE_WRITE_THROUGH)) {
        error.clear();
        return true;
    }
    const auto nativeError = GetLastError();
    if (nativeError == ERROR_ALREADY_EXISTS ||
        nativeError == ERROR_FILE_EXISTS) {
        error = std::make_error_code(std::errc::file_exists);
    } else if (nativeError == ERROR_ACCESS_DENIED) {
        std::error_code existsError;
        if (std::filesystem::exists(destination, existsError)) {
            error = std::make_error_code(std::errc::file_exists);
        } else if (existsError) {
            error = existsError;
        } else {
            error = std::error_code(static_cast<int>(nativeError),
                                    std::system_category());
        }
    } else {
        error = std::error_code(static_cast<int>(nativeError),
                                std::system_category());
    }
    return false;
#else
    std::filesystem::create_hard_link(source, destination, error);
    if (error) return false;

    const bool removed = std::filesystem::remove(source, error);
    if (!error && removed) return true;
    if (!error)
        error = std::make_error_code(std::errc::no_such_file_or_directory);

    // If unlinking the source failed, remove the newly-created link when
    // possible. The source remains the authoritative copy either way.
    std::error_code cleanupError;
    std::filesystem::remove(destination, cleanupError);
    return false;
#endif
}

} // namespace cppfm::storage_file_ops
