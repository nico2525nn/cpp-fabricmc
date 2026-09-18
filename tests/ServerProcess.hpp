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
#include <string>
#include <system_error>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace cpptest {

struct ServerProcessOptions {
    using PortProbe = bool (*)(std::uint16_t);

    std::uint16_t portBase = 26000;
    std::uint16_t portSpan = 3000;
    int viewDistance = 6;
    // Most protocol fixtures use the legacy flat-world coordinates (surface
    // at y=-61). Keep that test fixture explicit now that the production
    // default matches vanilla normal terrain.
    std::string levelType = "flat";
    bool onlineMode = false;
    std::string motd;
    std::string worldPrefix = "/tmp/cppfm-test-";
    int readyTimeoutMs = 30000;
    bool isolateRuntime = false;
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
        const auto span = options.portSpan == 0 ? 1 : options.portSpan;
        port = static_cast<std::uint16_t>(
            options.portBase + (static_cast<unsigned>(getpid()) % span));
        worldDir = options.worldPrefix + std::to_string(getpid());
        if (!prepareWorld()) return false;

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

        pid = fork();
        if (pid < 0) {
            cleanupWorld();
            return false;
        }
        if (pid == 0) {
            if (options.isolateRuntime)
                (void)setenv("CPPFM_SERVER_DIR", worldDir.c_str(), 1);

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
            const std::string motdArg = "--motd=" + options.motd;
            if (options.motd.empty()) {
                execl(serverPath, serverPath, portArg, viewArg, worldArg,
                      levelArg.c_str(), onlineArg, static_cast<char*>(nullptr));
            } else {
                execl(serverPath, serverPath, portArg, viewArg, worldArg,
                      levelArg.c_str(), onlineArg, motdArg.c_str(),
                      static_cast<char*>(nullptr));
            }
            _exit(127);
        }

        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(std::max(0, options.readyTimeoutMs));
        while (std::chrono::steady_clock::now() < deadline) {
            int childStatus = 0;
            const pid_t result = waitpid(pid, &childStatus, WNOHANG);
            if (result == pid || (result < 0 && errno == ECHILD)) {
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
        stop();
        return false;
    }

    bool stop() noexcept {
        const pid_t child = pid;
        bool reaped = child <= 0;
        bool signalOk = true;
        if (child > 0) {
            if (::kill(child, SIGTERM) < 0 && errno != ESRCH) signalOk = false;
            int status = 0;
            for (int i = 0; i < 600; ++i) {
                const pid_t result = waitpid(child, &status, WNOHANG);
                if (result == child) {
                    reaped = true;
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
                if (::kill(child, SIGKILL) < 0 && errno != ESRCH) signalOk = false;
                for (;;) {
                    const pid_t result = waitpid(child, &status, 0);
                    if (result == child) {
                        reaped = true;
                        break;
                    }
                    if (result < 0 && errno == EINTR) continue;
                    signalOk = false;
                    break;
                }
            }
        }
        if (reaped) pid = -1;
        const bool worldRemoved = reaped && cleanupWorld();
        if (!reaped || !signalOk || !worldRemoved) {
            std::fprintf(stderr,
                         "[ServerProcess] cleanup failed (pid=%ld reaped=%s signal=%s world=%s)\n",
                         static_cast<long>(child), reaped ? "yes" : "no",
                         signalOk ? "yes" : "no", worldRemoved ? "yes" : "no");
            return false;
        }
        return true;
    }

private:
    bool prepareWorld() {
        std::error_code ec;
        std::filesystem::remove_all(worldDir, ec);
        if (ec) return false;
        std::filesystem::create_directories(worldDir, ec);
        return !ec;
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
};

using ServerProc = ServerProcess;

} // namespace cpptest
