// The file contains a PID/timestamp record and refuses a second live server
// on the same world.  The PID is useful for diagnostics, while the OS file
// lock is the actual ownership primitive.  A stale PID is harmless once its
// advisory lock has been released; a live lock is never overwritten.
#pragma once
#include <charconv>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
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
#include <sys/file.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace cppfm {

class SessionLock {
public:
    SessionLock() = default;
    SessionLock(const SessionLock&) = delete;
    SessionLock& operator=(const SessionLock&) = delete;
    ~SessionLock() { release(); }

    std::string lockPath(const std::string& worldDir) const {
        return worldDir + "/session.lock";
    }

    // Parse "pid timestampMs" content. Returns pid or -1.
    static long parsePid(const std::string& content) {
        const std::size_t end = content.find_first_of(" \t\r\n");
        const std::string_view token(content.data(),
                                     end == std::string::npos ? content.size() : end);
        if (token.empty()) return -1;
        long pid = 0;
        const auto result = std::from_chars(token.data(), token.data() + token.size(), pid, 10);
        if (result.ec != std::errc{} || result.ptr != token.data() + token.size() || pid <= 0)
            return -1;
        return pid;
    }

    static bool pidAlive(long pid) {
#ifdef _WIN32
        if (pid <= 0) return false;
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                           static_cast<DWORD>(pid));
        if (process) {
            CloseHandle(process);
            return true;
        }
        return GetLastError() == ERROR_ACCESS_DENIED;
#elif defined(__unix__)
        if (pid <= 0) return false;
        if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
        return errno != ESRCH; // EPERM => exists but not ours
#else
        return false;
#endif
    }

    static long selfPid() {
#ifdef _WIN32
        return static_cast<long>(GetCurrentProcessId());
#elif defined(__unix__)
        return static_cast<long>(::getpid());
#else
        return -1;
#endif
    }

    static std::int64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    // Acquire the lock.  A live holder is reported through heldByLiveOther and
    // causes a clean failure.  I/O and permission errors also fail closed;
    // continuing without a lock can corrupt level.dat and region files.
    bool acquire(const std::string& worldDir, bool& heldByLiveOther) {
        heldByLiveOther = false;
        if (held_) return dir_ == worldDir;
        dir_ = worldDir;
        try {
            std::filesystem::create_directories(worldDir);
            const std::string p = lockPath(worldDir);
#ifdef _WIN32
            const HANDLE handle = CreateFileW(
                std::filesystem::path(p).wstring().c_str(),
                GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                const auto winError = GetLastError();
                heldByLiveOther = winError == ERROR_SHARING_VIOLATION ||
                                  winError == ERROR_LOCK_VIOLATION;
                std::fprintf(stderr, "[cppfm] ERROR: could not open %s (Win32 error %lu)%s\n",
                             p.c_str(), static_cast<unsigned long>(winError),
                             heldByLiveOther ? " (another server is running)" : "");
                dir_.clear();
                return false;
            }
            fd_ = handle;
            LARGE_INTEGER zero{};
            bool ok = SetFilePointerEx(fd_, zero, nullptr, FILE_BEGIN) &&
                      SetEndOfFile(fd_);
            const std::string record = std::to_string(selfPid()) + " " +
                                       std::to_string(nowMs()) + "\n";
            DWORD written = 0;
            if (ok) {
                ok = WriteFile(fd_, record.data(), static_cast<DWORD>(record.size()),
                               &written, nullptr) &&
                     written == static_cast<DWORD>(record.size());
            }
            if (ok) ok = FlushFileBuffers(fd_) != FALSE;
            if (!ok) {
                std::fprintf(stderr, "[cppfm] ERROR: could not write %s (Win32 error %lu)\n",
                             p.c_str(), static_cast<unsigned long>(GetLastError()));
                CloseHandle(fd_);
                fd_ = INVALID_HANDLE_VALUE;
                dir_.clear();
                return false;
            }
            held_ = true;
            return true;
#elif defined(__unix__)
            const int fd = ::open(p.c_str(), O_RDWR | O_CREAT, 0644);
            if (fd < 0) {
                std::fprintf(stderr, "[cppfm] ERROR: could not open %s: %s\n",
                             p.c_str(), std::strerror(errno));
                dir_.clear();
                return false;
            }
            if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
                heldByLiveOther = (errno == EWOULDBLOCK || errno == EAGAIN);
                std::fprintf(stderr,
                             "[cppfm] ERROR: session.lock is already held for %s%s\n",
                             worldDir.c_str(), heldByLiveOther ? " (another server is running)" : "");
                ::close(fd);
                dir_.clear();
                return false;
            }
            fd_ = fd;
            if (::ftruncate(fd_, 0) != 0 ||
                ::lseek(fd_, 0, SEEK_SET) < 0) {
                std::fprintf(stderr, "[cppfm] ERROR: could not prepare %s: %s\n",
                             p.c_str(), std::strerror(errno));
                ::flock(fd_, LOCK_UN);
                ::close(fd_);
                fd_ = -1;
                dir_.clear();
                return false;
            }
            const std::string record = std::to_string(selfPid()) + " " +
                                       std::to_string(nowMs()) + "\n";
            std::size_t offset = 0;
            bool writeOk = true;
            while (offset < record.size()) {
                const ssize_t written = ::write(fd_, record.data() + offset,
                                                record.size() - offset);
                if (written < 0 && errno == EINTR) continue;
                if (written <= 0) {
                    writeOk = false;
                    break;
                }
                offset += static_cast<std::size_t>(written);
            }
            if (!writeOk || ::fsync(fd_) != 0) {
                std::fprintf(stderr, "[cppfm] ERROR: could not write %s: %s\n",
                             p.c_str(), std::strerror(errno));
                ::flock(fd_, LOCK_UN);
                ::close(fd_);
                fd_ = -1;
                dir_.clear();
                return false;
            }
            held_ = true;
            return true;
#else
            if (std::filesystem::exists(p)) {
                std::ifstream f(p, std::ios::binary);
                std::string content((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
                const long owner = parsePid(content);
                const long me = selfPid();
                if (owner > 0 && owner != me && pidAlive(owner)) {
                    heldByLiveOther = true;
                    std::fprintf(stderr,
                                 "[cppfm] ERROR: session.lock held by live pid %ld; "
                                 "another server is running on %s\n",
                                 owner, worldDir.c_str());
                    dir_.clear();
                    return false;
                } else {
                    std::fprintf(stderr,
                                 "[cppfm] stale session.lock (pid %ld) overwritten for %s\n",
                                 owner, worldDir.c_str());
                }
            }
            std::ofstream o(p, std::ios::binary | std::ios::trunc);
            if (o) {
                o << selfPid() << " " << nowMs() << "\n";
                held_ = static_cast<bool>(o);
            }
            if (!held_)
                std::fprintf(stderr, "[cppfm] ERROR: could not write %s\n", p.c_str());
            if (!held_) dir_.clear();
            return held_;
#endif
        } catch (...) {
            dir_.clear();
            return false;
        }
    }

    bool acquire(const std::string& worldDir) {
        bool dummy = false;
        return acquire(worldDir, dummy);
    }

    void release() {
        if (!held_ || dir_.empty()) return;
        held_ = false;
#ifdef _WIN32
        if (fd_ != INVALID_HANDLE_VALUE) {
            CloseHandle(fd_);
            fd_ = INVALID_HANDLE_VALUE;
        }
#elif defined(__unix__)
        const int fd = fd_;
        fd_ = -1;
        // Keep the inode in place.  Unlinking before releasing the advisory
        // lock lets a new process create a different inode and bypass the
        // lock during the hand-off race.  The next owner overwrites the
        // diagnostic record while holding the same inode lock.
        if (fd >= 0) {
            ::flock(fd, LOCK_UN);
            ::close(fd);
        }
#else
        try {
            // The non-POSIX fallback has no kernel-held lock, so remove only
            // our own record after verifying its PID.
            std::ifstream f(lockPath(dir_), std::ios::binary);
            if (f) {
                std::string content((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
                if (parsePid(content) == selfPid()) {
                    std::error_code ec;
                    std::filesystem::remove(lockPath(dir_), ec);
                }
            }
        } catch (...) {}
#endif
        dir_.clear();
    }

    bool held() const { return held_; }

private:
    std::string dir_;
    bool held_ = false;
#ifdef _WIN32
    HANDLE fd_ = INVALID_HANDLE_VALUE;
#elif defined(__unix__)
    int fd_ = -1;
#endif
};

} // namespace cppfm
