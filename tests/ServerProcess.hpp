#pragma once

// Shared POSIX live-server harness for integration tests.  The options keep
// each suite's protocol scenario independent while the process-ownership and
// timeout rules stay in one implementation.
#include "TestClient.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace cpptest {

inline std::filesystem::path temporaryTestPath(std::string_view name) {
    return std::filesystem::temp_directory_path() / std::string(name);
}

inline std::string temporaryWorldPrefix(std::string_view label) {
    return temporaryTestPath(std::string(label) + "-").string();
}

struct ServerProcessOptions {
    using PortProbe = bool (*)(std::uint16_t);

    std::uint16_t portBase = 26000;
    std::uint16_t portSpan = 3000;
    int viewDistance = 6;
    int gracefulStopTimeoutMs = 60000;
    // Most protocol fixtures use the legacy flat-world coordinates (surface
    // at y=-61). Keep that test fixture explicit now that the production
    // default matches vanilla normal terrain.
    std::string levelType = "flat";
    // These live protocol fixtures use fake/offline clients. Keep both
    // authentication settings explicit rather than inheriting server defaults.
    bool onlineMode = false;
    bool enforceSecureProfile = false;
    bool jvmEnabled = true;
    std::optional<int> maxPlayers;
    std::string motd;
    std::string worldPrefix = temporaryWorldPrefix("cppfm-test");
    int readyTimeoutMs = 30000;
    std::vector<std::string> operatorNames;
    bool isolateRuntime = false;
    bool authStub = false;
    // A live test may keep a raw TCP readiness policy.  The common owner still
    // owns port collision probing, fork/exec, and the bounded child lifecycle.
    PortProbe portProbe = nullptr;
};

class ServerProcess {
public:
    pid_t pid = -1;
    std::uint16_t port = 0;
    std::string worldDir;

    ~ServerProcess() {
        // A destructor cannot return a gate result.  Do not silently turn an
        // unreaped child or failed temporary-world cleanup into a passing
        // test when an early return skipped the explicit CHECK(stop()).
        if (!stop()) std::abort();
    }

    bool start(const char* serverPath,
               const ServerProcessOptions& options = ServerProcessOptions{}) {
        childExitStatus_.reset();
        childWaitError_ = 0;
        gracefulStopTimeoutMs_ = std::max(0, options.gracefulStopTimeoutMs);
        const auto span = options.portSpan == 0 ? 1 : options.portSpan;
        port = static_cast<std::uint16_t>(
            options.portBase + (static_cast<unsigned>(getpid()) % span));
        worldDir = options.worldPrefix + std::to_string(getpid());
        if (!prepareWorld(options.operatorNames)) return false;

        const auto probePort = [&](std::uint16_t candidate) {
            if (options.portProbe) return options.portProbe(candidate);
            TestClient probe;
            if (!probe.connect("127.0.0.1", candidate, 1)) return false;
            probe.close();
            return true;
        };
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (!probePort(port)) break;
            ++port;
        }

        const std::string serverPathAbs =
            std::filesystem::absolute(serverPath).string();
        pid = fork();
        if (pid < 0) {
            cleanupWorld();
            return false;
        }
        if (pid == 0) {
            if (::chdir(worldDir.c_str()) != 0) _exit(126);
            if (options.isolateRuntime)
                (void)setenv("CPPFM_SERVER_DIR", worldDir.c_str(), 1);
            if (options.authStub)
                (void)setenv("CPPFM_AUTH_STUB", "1", 1);

            char portArg[32];
            char viewArg[32];
            char worldArg[256];
            const std::string levelArg = "--level-type=" + options.levelType;
            std::snprintf(portArg, sizeof(portArg), "--port=%u", port);
            std::snprintf(viewArg, sizeof(viewArg), "--view-distance=%d",
                          options.viewDistance);
            std::snprintf(worldArg, sizeof(worldArg), "--world-dir=%s",
                          worldDir.c_str());
            const char* onlineArg = options.onlineMode
                ? "--online-mode=true" : "--online-mode=false";
            const char* secureProfileArg = options.enforceSecureProfile
                ? "--enforce-secure-profile=true" : "--enforce-secure-profile=false";
            const char* jvmArg = options.jvmEnabled
                ? "--jvm-enabled=true" : "--jvm-enabled=false";
            const std::string motdArg = "--motd=" + options.motd;
            const std::string maxPlayersArg = options.maxPlayers
                ? "--max-players=" + std::to_string(*options.maxPlayers) : std::string{};
            if (options.motd.empty()) {
                if (options.maxPlayers) {
                    execl(serverPathAbs.c_str(), serverPathAbs.c_str(), portArg, viewArg,
                          worldArg, levelArg.c_str(), onlineArg, secureProfileArg,
                          jvmArg, maxPlayersArg.c_str(), static_cast<char*>(nullptr));
                } else {
                    execl(serverPathAbs.c_str(), serverPathAbs.c_str(), portArg, viewArg,
                          worldArg, levelArg.c_str(), onlineArg, secureProfileArg, jvmArg,
                          static_cast<char*>(nullptr));
                }
            } else {
                if (options.maxPlayers) {
                    execl(serverPathAbs.c_str(), serverPathAbs.c_str(), portArg, viewArg,
                          worldArg, levelArg.c_str(), onlineArg, secureProfileArg,
                          jvmArg, motdArg.c_str(), maxPlayersArg.c_str(),
                          static_cast<char*>(nullptr));
                } else {
                    execl(serverPathAbs.c_str(), serverPathAbs.c_str(), portArg, viewArg,
                          worldArg, levelArg.c_str(), onlineArg, secureProfileArg,
                          jvmArg, motdArg.c_str(), static_cast<char*>(nullptr));
                }
            }
            _exit(127);
        }

        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(std::max(0, options.readyTimeoutMs));
        while (std::chrono::steady_clock::now() < deadline) {
            int childStatus = 0;
            const pid_t result = waitpid(pid, &childStatus, WNOHANG);
            if (result == pid) {
                childExitStatus_ = childStatus;
                std::fprintf(stderr,
                    "[ServerProcess] child exited before readiness "
                    "(pid=%ld exit=%d signal=%d port=%u)\n",
                    static_cast<long>(pid),
                    WIFEXITED(childStatus) ? WEXITSTATUS(childStatus) : -1,
                    WIFSIGNALED(childStatus) ? WTERMSIG(childStatus) : -1,
                    static_cast<unsigned>(port));
                pid = -1;
                (void)cleanupWorld();
                return false;
            }
            if (result < 0 && errno == ECHILD) {
                childWaitError_ = errno;
                std::fprintf(stderr,
                    "[ServerProcess] child was reaped before readiness "
                    "(port=%u world=%s)\n",
                    static_cast<unsigned>(port), worldDir.c_str());
                pid = -1;
                (void)cleanupWorld();
                return false;
            }
            if (result < 0 && errno != EINTR) {
                stop();
                return false;
            }

            if (probePort(port)) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::fprintf(stderr,
            "[ServerProcess] readiness timeout after %d ms "
            "(pid=%ld port=%u world=%s)\n",
            std::max(0, options.readyTimeoutMs), static_cast<long>(pid),
            static_cast<unsigned>(port), worldDir.c_str());
        (void)stop();
        return false;
    }

    // Reap a server that exited unexpectedly so a live integration suite can
    // stop after the scenario that lost its server instead of timing out in
    // every later scenario. A returned true means the child is no longer
    // owned by waitpid, either because it exited or because reaping failed.
    bool pollChildExit() noexcept {
        if (pid <= 0)
            return childExitStatus_.has_value() || childWaitError_ != 0;

        int status = 0;
        pid_t result = -1;
        do {
            result = waitpid(pid, &status, WNOHANG);
        } while (result < 0 && errno == EINTR);

        if (result == 0) return false;
        if (result == pid) {
            childExitStatus_ = status;
            pid = -1;
            return true;
        }

        childWaitError_ = errno;
        pid = -1;
        return true;
    }

    [[nodiscard]] const std::optional<int>& childExitStatus() const noexcept {
        return childExitStatus_;
    }

    [[nodiscard]] int childWaitError() const noexcept { return childWaitError_; }

    bool stop() noexcept {
        const pid_t child = pid;
        bool reaped = child <= 0;
        bool signalOk = true;
        bool forcedTermination = false;
        if (child > 0) {
            if (::kill(child, SIGTERM) < 0 && errno != ESRCH) signalOk = false;
            int status = 0;
            const int gracefulWaitAttempts = (gracefulStopTimeoutMs_ + 99) / 100;
            for (int i = 0; i < gracefulWaitAttempts; ++i) {
                const pid_t result = waitpid(child, &status, WNOHANG);
                if (result == child) {
                    reaped = true;
                    childExitStatus_ = status;
                    break;
                }
                if (result < 0) {
                    if (errno == EINTR) {
                        --i;
                        continue;
                    }
                    reaped = false;
                    signalOk = false;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!reaped) {
                forcedTermination = true;
                if (::kill(child, SIGKILL) < 0 && errno != ESRCH) signalOk = false;
                for (;;) {
                    const pid_t result = waitpid(child, &status, 0);
                    if (result == child) {
                        reaped = true;
                        childExitStatus_ = status;
                        break;
                    }
                    if (result < 0 && errno == EINTR) continue;
                    if (result < 0) childWaitError_ = errno;
                    signalOk = false;
                    break;
                }
            }
        }
        if (reaped) pid = -1;
        const bool worldRemoved = reaped && cleanupWorld();
        if (forcedTermination) {
            std::fprintf(stderr,
                         "[ServerProcess] child required forced termination after %d ms\n",
                         gracefulStopTimeoutMs_);
        }
        if (!reaped || !signalOk || !worldRemoved || forcedTermination) {
            std::fprintf(stderr,
                         "[ServerProcess] cleanup failed (pid=%ld reaped=%s signal=%s world=%s)\n",
                         static_cast<long>(child), reaped ? "yes" : "no",
                         signalOk ? "yes" : "no", worldRemoved ? "yes" : "no");
            return false;
        }
        return true;
    }

private:
    bool prepareWorld(const std::vector<std::string>& operatorNames) {
        std::error_code ec;
        std::filesystem::remove_all(worldDir, ec);
        if (ec) return false;
        std::filesystem::create_directories(worldDir, ec);
        if (ec) return false;
        if (!operatorNames.empty()) {
            std::ofstream ops(worldDir + "/ops.json", std::ios::trunc);
            if (!ops) return false;
            ops << '[';
            for (std::size_t i = 0; i < operatorNames.size(); ++i) {
                if (i != 0) ops << ',';
                ops << "{\"name\":\"" << operatorNames[i]
                    << "\",\"level\":4,\"bypassesPlayerLimit\":false}";
            }
            ops << ']';
            if (!ops) return false;
        }
        return true;
    }

    bool cleanupWorld() noexcept {
        if (worldDir.empty()) return true;
        std::error_code ec;
        std::filesystem::remove_all(worldDir, ec);
        if (ec) {
            std::fprintf(stderr, "[ServerProcess] world cleanup failed for %s: %s\n",
                         worldDir.c_str(), ec.message().c_str());
            return false;
        }
        return true;
    }

    int gracefulStopTimeoutMs_ = 60000;
    std::optional<int> childExitStatus_;
    int childWaitError_ = 0;
};

using ServerProc = ServerProcess;

} // namespace cpptest
