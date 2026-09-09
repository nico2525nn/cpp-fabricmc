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
    std::uint16_t portBase = 26000;
    std::uint16_t portSpan = 3000;
    int viewDistance = 6;
    bool onlineMode = false;
    std::string motd;
    std::string worldPrefix = "/tmp/cppfm-test-";
    int readyTimeoutMs = 30000;
    bool isolateRuntime = false;
};

class ServerProcess {
public:
    pid_t pid = -1;
    std::uint16_t port = 0;
    std::string worldDir;

    ~ServerProcess() { stop(); }

    bool start(const char* serverPath,
               const ServerProcessOptions& options = ServerProcessOptions{}) {
        const auto span = options.portSpan == 0 ? 1 : options.portSpan;
        port = static_cast<std::uint16_t>(
            options.portBase + (static_cast<unsigned>(getpid()) % span));
        worldDir = options.worldPrefix + std::to_string(getpid());
        if (!prepareWorld()) return false;

        for (int attempt = 0; attempt < 20; ++attempt) {
            TestClient probe;
            if (!probe.connect("127.0.0.1", port, 1)) break;
            probe.close();
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
                      onlineArg, static_cast<char*>(nullptr));
            } else {
                execl(serverPath, serverPath, portArg, viewArg, worldArg,
                      onlineArg, motdArg.c_str(), static_cast<char*>(nullptr));
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
                cleanupWorld();
                return false;
            }
            if (result < 0 && errno != EINTR) {
                stop();
                return false;
            }

            TestClient probe;
            if (probe.connect("127.0.0.1", port, 1)) {
                probe.close();
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        stop();
        return false;
    }

    void stop() noexcept {
        const pid_t child = pid;
        pid = -1;
        bool reaped = child <= 0;
        if (child > 0) {
            (void)kill(child, SIGTERM);
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
                    reaped = errno == ECHILD;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!reaped) {
                (void)kill(child, SIGKILL);
                for (;;) {
                    const pid_t result = waitpid(child, &status, 0);
                    if (result == child || (result < 0 && errno == ECHILD)) {
                        reaped = true;
                        break;
                    }
                    if (result < 0 && errno == EINTR) continue;
                    break;
                }
            }
        }
        if (reaped) cleanupWorld();
    }

private:
    bool prepareWorld() {
        std::error_code ec;
        std::filesystem::remove_all(worldDir, ec);
        if (ec) return false;
        std::filesystem::create_directories(worldDir, ec);
        return !ec;
    }

    void cleanupWorld() noexcept {
        if (worldDir.empty()) return;
        std::error_code ec;
        std::filesystem::remove_all(worldDir, ec);
    }
};

using ServerProc = ServerProcess;

} // namespace cpptest
