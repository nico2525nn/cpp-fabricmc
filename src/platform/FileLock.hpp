// Small cross-platform advisory process lock used for server-directory setup.
#pragma once

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace cppfm::platform {

class FileLock {
public:
    FileLock() = default;

    explicit FileLock(const std::filesystem::path& path, std::string& error) {
        acquire(path, error);
    }

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

    ~FileLock() { release(); }

    bool acquire(const std::filesystem::path& path, std::string& error) {
        release();
#ifdef _WIN32
        handle_ = CreateFileW(
            path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            error = "could not open server lock " + path.string() +
                    ": error " + std::to_string(static_cast<unsigned long>(GetLastError()));
            return false;
        }
        OVERLAPPED offset{};
        if (!LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                        0, MAXDWORD, MAXDWORD, &offset)) {
            const auto code = static_cast<unsigned long>(GetLastError());
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            error = code == ERROR_LOCK_VIOLATION
                        ? "server directory is already in use: " + path.parent_path().parent_path().string()
                        : "could not lock server directory " + path.string() +
                              ": error " + std::to_string(code);
            return false;
        }
        locked_ = true;
#else
        fd_ = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW,
                     0666);
        if (fd_ < 0) {
            error = "could not open server lock " + path.string() +
                    ": " + std::string(std::strerror(errno));
            return false;
        }
        struct stat status {};
        const int statResult = ::fstat(fd_, &status);
        if (statResult != 0 || !S_ISREG(status.st_mode)) {
            const int code = statResult != 0 ? errno : EINVAL;
            ::close(fd_);
            fd_ = -1;
            error = "server lock is not a regular file " + path.string() +
                    ": " + std::string(std::strerror(code));
            return false;
        }
        if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            const int code = errno;
            ::close(fd_);
            fd_ = -1;
            error = (code == EWOULDBLOCK || code == EAGAIN)
                        ? "server directory is already in use: " + path.parent_path().parent_path().string()
                        : "could not lock server directory " + path.string() +
                              ": " + std::string(std::strerror(code));
            return false;
        }
        locked_ = true;
#endif
        return true;
    }

#ifndef _WIN32
    // Resolve the lock relative to an already validated parent directory.
    // This avoids re-resolving a replaceable path component after RuntimeLayout
    // has rejected symlinked server directories.
    bool acquireAt(int parentFd, const std::string& filename,
                   const std::filesystem::path& displayPath,
                   std::string& error) {
        release();
        if (filename.empty() || filename == "." || filename == ".." ||
            filename.find('/') != std::string::npos) {
            error = "invalid server lock filename: " + filename;
            return false;
        }
        fd_ = ::openat(parentFd, filename.c_str(),
                       O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0666);
        if (fd_ < 0) {
            error = "could not open server lock " + displayPath.string() +
                    ": " + std::string(std::strerror(errno));
            return false;
        }
        struct stat status {};
        const int statResult = ::fstat(fd_, &status);
        if (statResult != 0 || !S_ISREG(status.st_mode)) {
            const int code = statResult != 0 ? errno : EINVAL;
            ::close(fd_);
            fd_ = -1;
            error = "server lock is not a regular file " +
                    displayPath.string() + ": " +
                    std::string(std::strerror(code));
            return false;
        }
        if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            const int code = errno;
            ::close(fd_);
            fd_ = -1;
            error = (code == EWOULDBLOCK || code == EAGAIN)
                ? "server directory is already in use: " +
                      displayPath.parent_path().parent_path().string()
                : "could not lock server directory " + displayPath.string() +
                      ": " + std::string(std::strerror(code));
            return false;
        }
        locked_ = true;
        return true;
    }
#endif

    bool locked() const noexcept { return locked_; }

private:
    void release() noexcept {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            if (locked_) {
                OVERLAPPED offset{};
                (void)UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &offset);
            }
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fd_ >= 0) {
            if (locked_) (void)::flock(fd_, LOCK_UN);
            (void)::close(fd_);
            fd_ = -1;
        }
#endif
        locked_ = false;
    }

#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
    bool locked_ = false;
};

} // namespace cppfm::platform
