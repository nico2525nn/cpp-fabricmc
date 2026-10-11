#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
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
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cppfm::storage_file_ops {

// Recovery copies are intentionally bounded even for dense files. Sparse
// sources need the same logical limit because a streaming copy materializes
// holes, but dense sources can exhaust the destination volume too. This is a
// recovery-copy policy, not a file-format or ordinary-read limit; the source
// remains untouched when a copy is refused.
inline constexpr std::uint64_t kMaxRecoveryCopyBytes =
    std::uint64_t{1} << 30;
inline constexpr std::uint64_t kMaxSparseBackupLogicalBytes =
    kMaxRecoveryCopyBytes;

#ifdef CPPFM_STORAGE_FILE_OPS_TESTING
inline std::atomic<unsigned>& directorySyncFailuresForTesting() {
    static std::atomic<unsigned> failures{0};
    return failures;
}

inline std::atomic<unsigned>& directorySyncsBeforeFailureForTesting() {
    static std::atomic<unsigned> count{0};
    return count;
}

inline void failNextDirectorySyncForTesting() noexcept {
    directorySyncFailuresForTesting().fetch_add(1, std::memory_order_release);
}

inline void failDirectorySyncAfterForTesting(unsigned successfulSyncs) noexcept {
    directorySyncsBeforeFailureForTesting().store(
        successfulSyncs, std::memory_order_release);
    failNextDirectorySyncForTesting();
}

inline bool skipDirectorySyncFailureForTesting() noexcept {
    auto& count = directorySyncsBeforeFailureForTesting();
    auto remaining = count.load(std::memory_order_acquire);
    while (remaining != 0) {
        if (count.compare_exchange_weak(
                remaining, remaining - 1, std::memory_order_acq_rel,
                std::memory_order_acquire))
            return true;
    }
    return false;
}

inline bool consumeDirectorySyncFailureForTesting() noexcept {
    auto& failures = directorySyncFailuresForTesting();
    auto remaining = failures.load(std::memory_order_acquire);
    while (remaining != 0) {
        if (failures.compare_exchange_weak(
                remaining, remaining - 1, std::memory_order_acq_rel,
                std::memory_order_acquire))
            return true;
    }
    return false;
}
#endif

enum class BoundedReadStatus { Read, Missing, NonRegular, TooLarge, Failed };

// Read one opened regular-file snapshot with a strict allocation cap. On
// POSIX O_NONBLOCK is essential: opening a FIFO read-only can otherwise wait
// forever before the caller has a chance to inspect its file type.
inline BoundedReadStatus readRegularFileBounded(
    const std::filesystem::path& path, std::size_t maxBytes,
    std::vector<std::uint8_t>& output, std::error_code& error) {
    output.clear();
#ifdef _WIN32
    const HANDLE file = CreateFileW(
        path.wstring().c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const auto nativeError = GetLastError();
        if (nativeError == ERROR_FILE_NOT_FOUND ||
            nativeError == ERROR_PATH_NOT_FOUND) {
            error = std::make_error_code(std::errc::no_such_file_or_directory);
            return BoundedReadStatus::Missing;
        }
        error = std::error_code(static_cast<int>(nativeError),
                                std::system_category());
        return BoundedReadStatus::Failed;
    }
    const DWORD fileType = GetFileType(file);
    if (fileType != FILE_TYPE_DISK) {
        CloseHandle(file);
        error = std::make_error_code(std::errc::invalid_argument);
        return BoundedReadStatus::NonRegular;
    }
    BY_HANDLE_FILE_INFORMATION fileInfo{};
    if (!GetFileInformationByHandle(file, &fileInfo)) {
        const auto nativeError = GetLastError();
        CloseHandle(file);
        error = std::error_code(static_cast<int>(nativeError),
                                std::system_category());
        return BoundedReadStatus::Failed;
    }
    if ((fileInfo.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        CloseHandle(file);
        error = std::make_error_code(std::errc::invalid_argument);
        return BoundedReadStatus::NonRegular;
    }
    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < 0) {
        const auto nativeError = GetLastError();
        CloseHandle(file);
        error = std::error_code(static_cast<int>(nativeError),
                                std::system_category());
        return BoundedReadStatus::Failed;
    }
    if (static_cast<unsigned long long>(fileSize.QuadPart) > maxBytes ||
        static_cast<unsigned long long>(fileSize.QuadPart) >
            std::numeric_limits<std::size_t>::max()) {
        CloseHandle(file);
        error = std::make_error_code(std::errc::file_too_large);
        return BoundedReadStatus::TooLarge;
    }
    output.resize(static_cast<std::size_t>(fileSize.QuadPart));
    std::size_t offset = 0;
    while (offset < output.size()) {
        const auto count = static_cast<DWORD>(std::min<std::size_t>(
            output.size() - offset, std::numeric_limits<DWORD>::max()));
        DWORD read = 0;
        if (!ReadFile(file, output.data() + offset, count, &read, nullptr) ||
            read == 0) {
            const auto nativeError = GetLastError();
            output.clear();
            CloseHandle(file);
            error = nativeError == ERROR_SUCCESS
                ? std::make_error_code(std::errc::io_error)
                : std::error_code(static_cast<int>(nativeError),
                                   std::system_category());
            return BoundedReadStatus::Failed;
        }
        offset += read;
    }
    if (!CloseHandle(file)) {
        output.clear();
        error = std::error_code(static_cast<int>(GetLastError()),
                                std::system_category());
        return BoundedReadStatus::Failed;
    }
#elif defined(__unix__)
    int flags = O_RDONLY;
#ifdef O_NONBLOCK
    flags |= O_NONBLOCK;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int file = ::open(path.c_str(), flags);
    if (file < 0) {
        if (errno == ENOENT || errno == ENOTDIR) {
            error = std::make_error_code(std::errc::no_such_file_or_directory);
            return BoundedReadStatus::Missing;
        }
        error = std::error_code(errno, std::generic_category());
        return BoundedReadStatus::Failed;
    }
    struct stat fileStatus {};
    if (::fstat(file, &fileStatus) != 0) {
        const int savedError = errno;
        ::close(file);
        error = std::error_code(savedError, std::generic_category());
        return BoundedReadStatus::Failed;
    }
    if (!S_ISREG(fileStatus.st_mode)) {
        ::close(file);
        error = std::make_error_code(std::errc::invalid_argument);
        return BoundedReadStatus::NonRegular;
    }
    if (fileStatus.st_size < 0 ||
        static_cast<std::uintmax_t>(fileStatus.st_size) > maxBytes ||
        static_cast<std::uintmax_t>(fileStatus.st_size) >
            std::numeric_limits<std::size_t>::max()) {
        ::close(file);
        error = std::make_error_code(std::errc::file_too_large);
        return BoundedReadStatus::TooLarge;
    }
    output.resize(static_cast<std::size_t>(fileStatus.st_size));
    std::size_t offset = 0;
    while (offset < output.size()) {
        const auto count = ::read(file, output.data() + offset,
                                  output.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            const int savedError = count == 0 ? EIO : errno;
            output.clear();
            ::close(file);
            error = std::error_code(savedError, std::generic_category());
            return BoundedReadStatus::Failed;
        }
        offset += static_cast<std::size_t>(count);
    }
    if (::close(file) != 0) {
        output.clear();
        error = std::error_code(errno, std::generic_category());
        return BoundedReadStatus::Failed;
    }
#else
    (void)path;
    (void)maxBytes;
    error = std::make_error_code(std::errc::operation_not_supported);
    return BoundedReadStatus::Failed;
#endif
    error.clear();
    return BoundedReadStatus::Read;
}

// Stream a regular file through an already-open handle. Opening nonblocking
// and validating the handle (rather than the path) keeps backup copies from
// hanging on FIFOs or following a path swapped to a symlink after a separate
// file-type check.
class RegularFileReader final {
public:
    enum class ReadStatus { Data, End, Failed };

    RegularFileReader() = default;
    ~RegularFileReader() { closeNoThrow(); }
    RegularFileReader(const RegularFileReader&) = delete;
    RegularFileReader& operator=(const RegularFileReader&) = delete;

    bool open(const std::filesystem::path& path, std::error_code& error) {
        if (isOpen()) {
            error = std::make_error_code(std::errc::device_or_resource_busy);
            return false;
        }
#ifdef _WIN32
        const HANDLE handle = CreateFileW(
            path.wstring().c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
                FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            error = std::error_code(static_cast<int>(GetLastError()),
                                    std::system_category());
            return false;
        }
        if (GetFileType(handle) != FILE_TYPE_DISK) {
            CloseHandle(handle);
            error = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        BY_HANDLE_FILE_INFORMATION attributes{};
        if (!GetFileInformationByHandle(handle, &attributes)) {
            const auto nativeError = GetLastError();
            CloseHandle(handle);
            error = std::error_code(static_cast<int>(nativeError),
                                    std::system_category());
            return false;
        }
        if ((attributes.dwFileAttributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
            CloseHandle(handle);
            error = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        FILE_STANDARD_INFO standardInfo{};
        if (!GetFileInformationByHandleEx(
                handle, FileStandardInfo, &standardInfo,
                static_cast<DWORD>(sizeof(standardInfo))) ||
            standardInfo.EndOfFile.QuadPart < 0 ||
            standardInfo.AllocationSize.QuadPart < 0) {
            const auto nativeError = GetLastError();
            CloseHandle(handle);
            error = std::error_code(static_cast<int>(nativeError),
                                    std::system_category());
            return false;
        }
        sizeBytes_ = static_cast<std::uint64_t>(
            standardInfo.EndOfFile.QuadPart);
        allocatedBytes_ = static_cast<std::uint64_t>(
            standardInfo.AllocationSize.QuadPart);
        handle_ = handle;
#elif defined(__unix__)
        int flags = O_RDONLY;
#ifdef O_NONBLOCK
        flags |= O_NONBLOCK;
#endif
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        fd_ = ::open(path.c_str(), flags);
        if (fd_ < 0) {
            error = std::error_code(errno, std::generic_category());
            return false;
        }
        struct stat fileStatus {};
        if (::fstat(fd_, &fileStatus) != 0) {
            const int savedError = errno;
            closeNoThrow();
            error = std::error_code(savedError, std::generic_category());
            return false;
        }
        if (!S_ISREG(fileStatus.st_mode)) {
            closeNoThrow();
            error = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        if (fileStatus.st_size < 0) {
            closeNoThrow();
            error = std::make_error_code(std::errc::value_too_large);
            return false;
        }
        sizeBytes_ = static_cast<std::uint64_t>(fileStatus.st_size);
        const auto allocatedBlocks = fileStatus.st_blocks > 0
            ? static_cast<std::uint64_t>(fileStatus.st_blocks) : 0;
        allocatedBytes_ = allocatedBlocks >
                std::numeric_limits<std::uint64_t>::max() / 512
            ? std::numeric_limits<std::uint64_t>::max()
            : allocatedBlocks * 512;
#else
        (void)path;
        error = std::make_error_code(std::errc::operation_not_supported);
        return false;
#endif
        error.clear();
        return true;
    }

    std::uint64_t sizeBytes() const noexcept { return sizeBytes_; }
    std::uint64_t allocatedBytes() const noexcept { return allocatedBytes_; }

    ReadStatus read(char* buffer, std::size_t capacity, std::size_t& count,
                    std::error_code& error) noexcept {
        count = 0;
        if (!isOpen() || (capacity != 0 && buffer == nullptr)) {
            error = std::make_error_code(std::errc::bad_file_descriptor);
            return ReadStatus::Failed;
        }
        if (capacity == 0) {
            error.clear();
            return ReadStatus::Data;
        }
#ifdef _WIN32
        const auto requested = static_cast<DWORD>(std::min<std::size_t>(
            capacity, std::numeric_limits<DWORD>::max()));
        DWORD readCount = 0;
        if (!ReadFile(handle_, buffer, requested, &readCount, nullptr)) {
            error = std::error_code(static_cast<int>(GetLastError()),
                                    std::system_category());
            return ReadStatus::Failed;
        }
        count = readCount;
#elif defined(__unix__)
        ssize_t readCount = 0;
        const auto requested = std::min<std::size_t>(
            capacity,
            static_cast<std::size_t>(
                std::numeric_limits<ssize_t>::max()));
        do {
            readCount = ::read(fd_, buffer, requested);
        } while (readCount < 0 && errno == EINTR);
        if (readCount < 0) {
            error = std::error_code(errno, std::generic_category());
            return ReadStatus::Failed;
        }
        count = static_cast<std::size_t>(readCount);
#else
        error = std::make_error_code(std::errc::operation_not_supported);
        return ReadStatus::Failed;
#endif
        error.clear();
        return count == 0 ? ReadStatus::End : ReadStatus::Data;
    }

    bool close(std::error_code& error) noexcept {
        if (!isOpen()) {
            error = std::make_error_code(std::errc::bad_file_descriptor);
            return false;
        }
#ifdef _WIN32
        const HANDLE handle = std::exchange(handle_, INVALID_HANDLE_VALUE);
        if (!CloseHandle(handle)) {
            error = std::error_code(static_cast<int>(GetLastError()),
                                    std::system_category());
            return false;
        }
#elif defined(__unix__)
        const int fd = std::exchange(fd_, -1);
        if (::close(fd) != 0) {
            error = std::error_code(errno, std::generic_category());
            return false;
        }
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
    std::uint64_t sizeBytes_ = 0;
    std::uint64_t allocatedBytes_ = 0;
};

// A random-access regular-file handle for formats such as Anvil regions.
// The path is checked on the handle opened with no-follow/reparse-point
// semantics, and all subsequent reads and writes stay on that same handle.
class RandomAccessRegularFile final {
public:
    enum class Access { ReadOnly, ReadWrite };

    RandomAccessRegularFile() = default;
    ~RandomAccessRegularFile() { closeNoThrow(); }
    RandomAccessRegularFile(const RandomAccessRegularFile&) = delete;
    RandomAccessRegularFile& operator=(const RandomAccessRegularFile&) = delete;

    bool open(const std::filesystem::path& path, Access access,
              bool createIfMissing, bool& createdOut,
              std::error_code& error, unsigned int posixMode = 0666) {
        createdOut = false;
        if (isOpen()) {
            error = std::make_error_code(std::errc::device_or_resource_busy);
            return false;
        }
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            if (openExisting(path, access, error)) {
                error.clear();
                return true;
            }
            const bool missing = error == std::errc::no_such_file_or_directory;
            if (!createIfMissing || !missing) return false;
            if (createNew(path, access, posixMode, error)) {
                createdOut = true;
                error.clear();
                return true;
            }
            if (error != std::errc::file_exists) return false;
        }
        error = std::make_error_code(std::errc::file_exists);
        return false;
    }

    bool size(std::uint64_t& output, std::error_code& error) const noexcept {
        if (!isOpen()) {
            error = std::make_error_code(std::errc::bad_file_descriptor);
            return false;
        }
#ifdef _WIN32
        LARGE_INTEGER value{};
        if (!GetFileSizeEx(handle_, &value) || value.QuadPart < 0) {
            error = std::error_code(static_cast<int>(GetLastError()),
                                    std::system_category());
            return false;
        }
        output = static_cast<std::uint64_t>(value.QuadPart);
#elif defined(__unix__)
        struct stat info {};
        if (::fstat(fd_, &info) != 0) {
            error = std::error_code(errno, std::generic_category());
            return false;
        }
        if (info.st_size < 0) {
            error = std::make_error_code(std::errc::value_too_large);
            return false;
        }
        output = static_cast<std::uint64_t>(info.st_size);
#else
        (void)output;
        error = std::make_error_code(std::errc::operation_not_supported);
        return false;
#endif
        error.clear();
        return true;
    }

    bool readAt(std::uint64_t offset, void* output, std::size_t size,
                std::error_code& error) noexcept {
        if (!isOpen() || (size != 0 && output == nullptr) ||
            offset > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()) ||
            size > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()) - offset) {
            error = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        auto* bytes = static_cast<std::uint8_t*>(output);
        std::size_t completed = 0;
        while (completed < size) {
#ifdef _WIN32
            LARGE_INTEGER position{};
            position.QuadPart = static_cast<LONGLONG>(offset + completed);
            if (!SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN)) {
                error = std::error_code(static_cast<int>(GetLastError()),
                                        std::system_category());
                return false;
            }
            const auto requested = static_cast<DWORD>(std::min<std::size_t>(
                size - completed, std::numeric_limits<DWORD>::max()));
            DWORD received = 0;
            if (!ReadFile(handle_, bytes + completed, requested, &received,
                          nullptr) || received == 0) {
                const auto nativeError = GetLastError();
                error = nativeError == ERROR_SUCCESS
                    ? std::make_error_code(std::errc::io_error)
                    : std::error_code(static_cast<int>(nativeError),
                                      std::system_category());
                return false;
            }
            completed += received;
#elif defined(__unix__)
            const auto requested = std::min<std::size_t>(
                size - completed,
                static_cast<std::size_t>(
                    std::numeric_limits<ssize_t>::max()));
            const auto received = ::pread(
                fd_, bytes + completed, requested,
                static_cast<off_t>(offset + completed));
            if (received < 0 && errno == EINTR) continue;
            if (received <= 0) {
                error = std::error_code(received == 0 ? EIO : errno,
                                        std::generic_category());
                return false;
            }
            completed += static_cast<std::size_t>(received);
#else
            (void)bytes;
            (void)completed;
            error = std::make_error_code(std::errc::operation_not_supported);
            return false;
#endif
        }
        error.clear();
        return true;
    }

    bool writeAt(std::uint64_t offset, const void* input, std::size_t size,
                 std::error_code& error) noexcept {
        if (!isOpen() || access_ != Access::ReadWrite ||
            (size != 0 && input == nullptr) ||
            offset > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()) ||
            size > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()) - offset) {
            error = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        const auto* bytes = static_cast<const std::uint8_t*>(input);
        std::size_t completed = 0;
        while (completed < size) {
#ifdef _WIN32
            LARGE_INTEGER position{};
            position.QuadPart = static_cast<LONGLONG>(offset + completed);
            if (!SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN)) {
                error = std::error_code(static_cast<int>(GetLastError()),
                                        std::system_category());
                return false;
            }
            const auto requested = static_cast<DWORD>(std::min<std::size_t>(
                size - completed, std::numeric_limits<DWORD>::max()));
            DWORD written = 0;
            if (!WriteFile(handle_, bytes + completed, requested, &written,
                           nullptr) || written == 0) {
                const auto nativeError = GetLastError();
                error = nativeError == ERROR_SUCCESS
                    ? std::make_error_code(std::errc::io_error)
                    : std::error_code(static_cast<int>(nativeError),
                                      std::system_category());
                return false;
            }
            completed += written;
#elif defined(__unix__)
            const auto requested = std::min<std::size_t>(
                size - completed,
                static_cast<std::size_t>(
                    std::numeric_limits<ssize_t>::max()));
            const auto written = ::pwrite(
                fd_, bytes + completed, requested,
                static_cast<off_t>(offset + completed));
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) {
                error = std::error_code(written == 0 ? EIO : errno,
                                        std::generic_category());
                return false;
            }
            completed += static_cast<std::size_t>(written);
#else
            (void)bytes;
            (void)completed;
            error = std::make_error_code(std::errc::operation_not_supported);
            return false;
#endif
        }
        error.clear();
        return true;
    }

    bool resize(std::uint64_t size, std::error_code& error) noexcept {
        if (!isOpen() || access_ != Access::ReadWrite ||
            size > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
            error = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
#ifdef _WIN32
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(size);
        if (!SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN) ||
            !SetEndOfFile(handle_)) {
            error = std::error_code(static_cast<int>(GetLastError()),
                                    std::system_category());
            return false;
        }
#elif defined(__unix__)
        if (::ftruncate(fd_, static_cast<off_t>(size)) != 0) {
            error = std::error_code(errno, std::generic_category());
            return false;
        }
#else
        (void)size;
        error = std::make_error_code(std::errc::operation_not_supported);
        return false;
#endif
        error.clear();
        return true;
    }

    bool sync(std::error_code& error) noexcept {
        if (!isOpen() || access_ != Access::ReadWrite) {
            error = std::make_error_code(std::errc::bad_file_descriptor);
            return false;
        }
#ifdef _WIN32
        if (!FlushFileBuffers(handle_)) {
            error = std::error_code(static_cast<int>(GetLastError()),
                                    std::system_category());
            return false;
        }
#elif defined(__unix__)
        int result = 0;
        do {
            result = ::fsync(fd_);
        } while (result != 0 && errno == EINTR);
        if (result != 0) {
            error = std::error_code(errno, std::generic_category());
            return false;
        }
#else
        error = std::make_error_code(std::errc::operation_not_supported);
        return false;
#endif
        error.clear();
        return true;
    }

    bool close(std::error_code& error) noexcept {
        if (!isOpen()) {
            error = std::make_error_code(std::errc::bad_file_descriptor);
            return false;
        }
#ifdef _WIN32
        const HANDLE handle = std::exchange(handle_, INVALID_HANDLE_VALUE);
        if (!CloseHandle(handle)) {
            error = std::error_code(static_cast<int>(GetLastError()),
                                    std::system_category());
            return false;
        }
#elif defined(__unix__)
        const int fd = std::exchange(fd_, -1);
        if (::close(fd) != 0) {
            error = std::error_code(errno, std::generic_category());
            return false;
        }
#endif
        error.clear();
        return true;
    }

private:
    bool openExisting(const std::filesystem::path& path, Access access,
                      std::error_code& error) {
#ifdef _WIN32
        const DWORD desiredAccess = GENERIC_READ |
            (access == Access::ReadWrite ? GENERIC_WRITE : 0);
        const HANDLE handle = CreateFileW(
            path.wstring().c_str(), desiredAccess,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const auto nativeError = GetLastError();
            error = (nativeError == ERROR_FILE_NOT_FOUND ||
                     nativeError == ERROR_PATH_NOT_FOUND)
                ? std::make_error_code(std::errc::no_such_file_or_directory)
                : std::error_code(static_cast<int>(nativeError),
                                  std::system_category());
            return false;
        }
        BY_HANDLE_FILE_INFORMATION info{};
        if (GetFileType(handle) != FILE_TYPE_DISK) {
            CloseHandle(handle);
            error = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        if (!GetFileInformationByHandle(handle, &info)) {
            const auto nativeError = GetLastError();
            CloseHandle(handle);
            error = std::error_code(static_cast<int>(nativeError),
                                    std::system_category());
            return false;
        }
        if ((info.dwFileAttributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
            CloseHandle(handle);
            error = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
        handle_ = handle;
#elif defined(__unix__)
        int flags = access == Access::ReadWrite ? O_RDWR : O_RDONLY;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NONBLOCK
        flags |= O_NONBLOCK;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        fd_ = ::open(path.c_str(), flags);
        if (fd_ < 0) {
            const int savedError = errno;
            error = (savedError == ENOENT || savedError == ENOTDIR)
                ? std::make_error_code(std::errc::no_such_file_or_directory)
                : std::error_code(savedError, std::generic_category());
            return false;
        }
        struct stat info {};
        if (::fstat(fd_, &info) != 0) {
            const int savedError = errno;
            closeNoThrow();
            error = std::error_code(savedError, std::generic_category());
            return false;
        }
        if (!S_ISREG(info.st_mode)) {
            closeNoThrow();
            error = std::make_error_code(std::errc::invalid_argument);
            return false;
        }
#else
        (void)path;
        (void)access;
        error = std::make_error_code(std::errc::operation_not_supported);
        return false;
#endif
        access_ = access;
        error.clear();
        return true;
    }

    bool createNew(const std::filesystem::path& path, Access access,
                   unsigned int posixMode, std::error_code& error) {
#ifdef _WIN32
        const DWORD desiredAccess = GENERIC_READ |
            (access == Access::ReadWrite ? GENERIC_WRITE : 0);
        const HANDLE handle = CreateFileW(
            path.wstring().c_str(), desiredAccess,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const auto nativeError = GetLastError();
            error = (nativeError == ERROR_ALREADY_EXISTS ||
                     nativeError == ERROR_FILE_EXISTS)
                ? std::make_error_code(std::errc::file_exists)
                : std::error_code(static_cast<int>(nativeError),
                                  std::system_category());
            return false;
        }
        handle_ = handle;
#elif defined(__unix__)
        int flags = O_CREAT | O_EXCL |
            (access == Access::ReadWrite ? O_RDWR : O_RDONLY);
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        fd_ = ::open(path.c_str(), flags, posixMode);
        if (fd_ < 0) {
            const int savedError = errno;
            error = savedError == EEXIST
                ? std::make_error_code(std::errc::file_exists)
                : std::error_code(savedError, std::generic_category());
            return false;
        }
#else
        (void)path;
        (void)access;
        (void)posixMode;
        error = std::make_error_code(std::errc::operation_not_supported);
        return false;
#endif
        access_ = access;
        error.clear();
        return true;
    }

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
    Access access_ = Access::ReadOnly;
};

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
                                unsigned int posixMode = 0666,
                                std::uint64_t maxBytes =
                                    kMaxRecoveryCopyBytes) {
    if (createdOut) *createdOut = false;
    RegularFileReader input;
    if (!input.open(source, error)) return false;
    if (input.sizeBytes() > maxBytes) {
        error = std::make_error_code(std::errc::file_too_large);
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
        std::uint64_t copiedBytes = 0;
        while (writeOk) {
            std::size_t bytesRead = 0;
            const auto readStatus = input.read(buffer.data(), buffer.size(),
                                               bytesRead, error);
            if (readStatus == RegularFileReader::ReadStatus::Failed) {
                writeOk = false;
                break;
            }
            if (readStatus == RegularFileReader::ReadStatus::End) break;
            if (bytesRead > maxBytes - copiedBytes) {
                error = std::make_error_code(std::errc::file_too_large);
                writeOk = false;
                break;
            }
            if (!writer.write(buffer.data(), bytesRead, error)) {
                writeOk = false;
                break;
            }
            copiedBytes += bytesRead;
        }
        if (writeOk) writeOk = input.close(error);
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
                                 unsigned int posixMode = 0666,
                                 std::uint64_t maxBytes =
                                     kMaxRecoveryCopyBytes) {
    if (createdOut) *createdOut = false;
    for (std::size_t suffix = 0; suffix < 10000; ++suffix) {
        auto candidate = base;
        if (suffix != 0) candidate += "." + std::to_string(suffix);
        createdPath = candidate;
        bool candidateCreated = false;
        if (copyFileExclusively(source, candidate, error,
                                &candidateCreated, posixMode, maxBytes)) {
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
#ifdef CPPFM_STORAGE_FILE_OPS_TESTING
    if (!skipDirectorySyncFailureForTesting() &&
        consumeDirectorySyncFailureForTesting())
        return false;
#endif
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

enum class MoveWithoutReplaceStatus {
    Failed,
    DestinationCreatedSourceRetained,
    Moved,
    MovedWithUncertainSourceRemoval
};

// Move a file only when the destination name is unused. Quarantine and
// recovery code must never replace an older evidence file. On Unix, the new
// hard-link entry is synced before removing the source, making the evidence
// copy durable before the active name can disappear. A post-unlink directory
// sync failure is reported separately because the destination was already
// made durable and must be retained.
inline MoveWithoutReplaceStatus moveFileWithoutReplace(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::error_code& error) {
#ifdef _WIN32
    if (MoveFileExW(source.wstring().c_str(), destination.wstring().c_str(),
                    MOVEFILE_WRITE_THROUGH)) {
        error.clear();
        return MoveWithoutReplaceStatus::Moved;
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
    return MoveWithoutReplaceStatus::Failed;
#else
    std::filesystem::create_hard_link(source, destination, error);
    if (error) return MoveWithoutReplaceStatus::Failed;

    const auto sourceParent = source.parent_path().empty()
        ? std::filesystem::path(".") : source.parent_path();
    const auto destinationParent = destination.parent_path().empty()
        ? std::filesystem::path(".") : destination.parent_path();
    if (!syncDirectory(destinationParent) ||
        (sourceParent != destinationParent && !syncDirectory(sourceParent))) {
        error = std::make_error_code(std::errc::io_error);
        return MoveWithoutReplaceStatus::DestinationCreatedSourceRetained;
    }

    const bool removed = std::filesystem::remove(source, error);
    if (!error && !removed)
        error = std::make_error_code(std::errc::no_such_file_or_directory);
    if (error) return MoveWithoutReplaceStatus::DestinationCreatedSourceRetained;

    if (!syncDirectory(sourceParent)) {
        error = std::make_error_code(std::errc::io_error);
        return MoveWithoutReplaceStatus::MovedWithUncertainSourceRemoval;
    }
    error.clear();
    return MoveWithoutReplaceStatus::Moved;
#endif
}

} // namespace cppfm::storage_file_ops
