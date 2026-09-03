// test_rcon_multi — plan46 §2 (O-09): RCON concurrency + auth.
// Self-contained (spawns RconServer in-process, no cppfm needed):
//  - 5 simultaneous RCON sessions, all commands answered
//  - 10 consecutive wrong-password auths rejected, server keeps serving
//  - post-flood correct auth + exec works (tick-equivalent liveness)
#include "../src/net/Rcon.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace cppfm;

#include "Harness.hpp"

static std::uint16_t freePort() {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    ::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    socklen_t n = sizeof(a);
    ::getsockname(s, reinterpret_cast<sockaddr*>(&a), &n);
    std::uint16_t p = ntohs(a.sin_port);
    ::close(s);
    return p;
}

struct RconClient {
    int fd = -1;
    bool open(const std::string& host, std::uint16_t port) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return false;
        sockaddr_in a{};
        a.sin_family = AF_INET;
        ::inet_pton(AF_INET, host.c_str(), &a.sin_addr);
        a.sin_port = htons(port);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
            ::close(fd); fd = -1; return false;
        }
        struct timeval tv{3, 0};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        return true;
    }
    void sendFrame(std::int32_t id, std::int32_t type, const std::string& body) {
        std::vector<std::uint8_t> f;
        std::uint32_t len = static_cast<std::uint32_t>(body.size() + 10);
        for (int i = 0; i < 4; ++i) f.push_back(static_cast<std::uint8_t>((len >> (8*i)) & 0xFF));
        for (int i = 0; i < 4; ++i) f.push_back(static_cast<std::uint8_t>((id >> (8*i)) & 0xFF));
        for (int i = 0; i < 4; ++i) f.push_back(static_cast<std::uint8_t>((type >> (8*i)) & 0xFF));
        f.insert(f.end(), body.begin(), body.end());
        f.push_back(0); f.push_back(0);
        std::size_t off = 0;
        while (off < f.size()) {
            ssize_t r = ::send(fd, f.data() + off, f.size() - off, MSG_NOSIGNAL);
            if (r <= 0) break;
            off += static_cast<std::size_t>(r);
        }
    }
    bool recvFrame(std::int32_t& id, std::int32_t& type, std::string& body) {
        std::uint8_t lb[4];
        std::size_t got = 0;
        while (got < 4) {
            ssize_t r = ::recv(fd, lb + got, 4 - got, 0);
            if (r <= 0) return false;
            got += static_cast<std::size_t>(r);
        }
        const std::uint32_t len = std::uint32_t(lb[0]) | (std::uint32_t(lb[1]) << 8) |
                                  (std::uint32_t(lb[2]) << 16) | (std::uint32_t(lb[3]) << 24);
        if (len > 8192 || len < 10) return false;
        std::vector<std::uint8_t> b(len);
        got = 0;
        while (got < len) {
            ssize_t r = ::recv(fd, b.data() + got, len - got, 0);
            if (r <= 0) return false;
            got += static_cast<std::size_t>(r);
        }
        std::memcpy(&id, b.data(), 4);
        std::memcpy(&type, b.data() + 4, 4);
        body.assign(reinterpret_cast<const char*>(b.data() + 8), len - 10);
        return true;
    }
    void close() { if (fd >= 0) { ::close(fd); fd = -1; } }
};

int main() {
    std::printf("=== test_rcon_multi — plan46 §2 O-09 ===\n");
    const std::uint16_t port = freePort();
    std::atomic<int> handled{0};
    RconConfig cfg;
    cfg.enabled = true;
    cfg.port = port;
    cfg.password = "s3cret-rcon-pw";
    RconServer srv(cfg, [&](const std::string& cmd) {
        ++handled;
        return std::string("echo:") + cmd;
    });
    CHECK(srv.start(), "setup: RconServer started");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // -- 1) 5 simultaneous sessions -------------------------------------------
    std::atomic<int> okCount{0};
    std::vector<std::thread> ts;
    for (int i = 0; i < 5; ++i) {
        ts.emplace_back([&, i] {
            RconClient c;
            if (!c.open("127.0.0.1", port)) return;
            c.sendFrame(1, 3, "s3cret-rcon-pw");
            std::int32_t id, type; std::string body;
            if (!c.recvFrame(id, type, body) || id == -1) { c.close(); return; }
            bool mine = true;
            for (int k = 0; k < 3; ++k) {
                const std::string cmd = "cmd" + std::to_string(i) + "_" + std::to_string(k);
                c.sendFrame(10 + k, 2, cmd);
                if (!c.recvFrame(id, type, body) || body != "echo:" + cmd) { mine = false; break; }
            }
            c.close();
            if (mine) ++okCount;
        });
    }
    for (auto& t : ts) t.join();
    CHECK(okCount.load() == 5, "rcon: 5 simultaneous sessions all answered (O-09)");

    // -- 2) 10 consecutive wrong passwords rejected, server keeps serving -----
    int rejected = 0;
    for (int i = 0; i < 10; ++i) {
        RconClient c;
        if (!c.open("127.0.0.1", port)) continue;
        c.sendFrame(1, 3, "wrong-pass");
        std::int32_t id, type; std::string body;
        if (c.recvFrame(id, type, body) && id == -1) ++rejected;
        c.close();
    }
    CHECK(rejected == 10, "rcon: 10x wrong password rejected with id=-1 (O-09)");

    // -- 3) post-flood liveness -------------------------------------------------
    {
        RconClient c;
        bool ok = c.open("127.0.0.1", port);
        auto t0 = std::chrono::steady_clock::now();
        if (ok) {
            c.sendFrame(1, 3, "s3cret-rcon-pw");
            std::int32_t id, type; std::string body;
            ok = c.recvFrame(id, type, body) && id != -1;
            if (ok) {
                c.sendFrame(2, 2, "list");
                ok = c.recvFrame(id, type, body) && body == "echo:list";
            }
            c.close();
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();
        CHECK(ok, "rcon: correct auth+exec works after flood (O-09)");
        CHECK(ms < 1000, "rcon: post-flood response <1s (tick unaffected)");
        CHECK(handled.load() == 5 * 3 + 1, "rcon: handler ran exactly 16 commands");
    }

    srv.stop();
    std::printf("test_rcon_multi: %d PASS %d FAIL\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
