// timestamp) and refuses a second live server on the same world. Our policy is availability- first: a lock held by a *live* PID is logged
// loudly (and reported by tools/check_world); a stale lock (dead PID, e.g. crash / SIGKILL / Docker restart with PID reuse risk) only warns
// and startup continues. The lock is always (re)written by the starting server and removed on clean shutdown.
#pragma once
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#ifdef __unix__
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
        try {
            std::size_t pos = 0;
            long pid = std::stol(content, &pos);
            return pid > 0 ? pid : -1;
        } catch (...) { return -1; }
    }

    static bool pidAlive(long pid) {
#ifdef __unix__
        if (pid <= 0) return false;
        if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
        return errno != ESRCH; // EPERM => exists but not ours
#else
        return false;
#endif
    }

    static long selfPid() {
#ifdef __unix__
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

    // Acquire the lock. Never throws. Returns true when startup may proceed (always true today — live-holder case is a loud warning, not a
    // refusal, so crash-recovery/restart harnesses can never deadlock on it). `heldByLiveOther` is set when another live process appears to
    // own it.
    bool acquire(const std::string& worldDir, bool& heldByLiveOther) {
        heldByLiveOther = false;
        dir_ = worldDir;
        try {
            std::filesystem::create_directories(worldDir);
            const std::string p = lockPath(worldDir);
            if (std::filesystem::exists(p)) {
                std::ifstream f(p, std::ios::binary);
                std::string content((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
                const long owner = parsePid(content);
                const long me = selfPid();
                if (owner > 0 && owner != me && pidAlive(owner)) {
                    heldByLiveOther = true;
                    std::fprintf(stderr,
                                 "[cppfm] WARNING: session.lock held by live pid %ld; "
                                 "another server may be running on %s (continuing)\n",
                                 owner, worldDir.c_str());
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
                std::fprintf(stderr, "[cppfm] WARNING: could not write %s\n", p.c_str());
            return true;
        } catch (...) {
            return true;
        }
    }

    bool acquire(const std::string& worldDir) {
        bool dummy = false;
        return acquire(worldDir, dummy);
    }

    void release() {
        if (!held_ || dir_.empty()) return;
        held_ = false;
        try {
            // Only remove our own lock: re-read pid first.
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
    }

    bool held() const { return held_; }

private:
    std::string dir_;
    bool held_ = false;
};

} // namespace cppfm
