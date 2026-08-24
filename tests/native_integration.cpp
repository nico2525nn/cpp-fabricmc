// Native end-to-end suite: spawns the real server binary and exercises it
// through TestClient (production framing code). Replaces the Python suites.
#include "TestClient.hpp"
#include <map>
#include "../src/core/NBT.hpp"
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

using namespace cppfm;
using namespace cpptest;

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    const bool c_ = static_cast<bool>(cond); \
    std::printf("  %s  %s\n", c_ ? " ok " : "FAIL", msg); \
    if (!c_) ++g_fail; } while (0)

// ------------------------------------------------------------------ helpers
static bool waitPort(std::uint16_t port, int timeoutMs) {
    for (int i = 0; i < timeoutMs / 100; ++i) {
        TestClient probe;
        if (probe.connect("127.0.0.1", port, 1)) { probe.close(); return true; }
        usleep(100 * 1000);
    }
    return false;
}

struct ServerProc {
    pid_t pid = -1;
    std::uint16_t port = 0;

    bool start(const char* serverPath, int viewDistance) {
        port = static_cast<std::uint16_t>(26000 + (getpid() % 3000));
        // pick a free-ish port by probing
        for (int attempt = 0; attempt < 20; ++attempt) {
            TestClient probe;
            if (!probe.connect("127.0.0.1", port, 1)) break;   // free
            probe.close();
            port = static_cast<std::uint16_t>(port + 1);
        }
        pid = fork();
        if (pid == 0) {
            char portArg[32], vdArg[32];
            snprintf(portArg, sizeof(portArg), "--port=%u", port);
            snprintf(vdArg, sizeof(vdArg), "--view-distance=%d", viewDistance);
            execl(serverPath, serverPath, portArg, vdArg, (char*)nullptr);
            _exit(127);
        }
        return waitPort(port, 8000);
    }
    void stop() {
        if (pid > 0) { kill(pid, SIGTERM); int st; waitpid(pid, &st, 0); pid = -1; }
    }
};

// Minimal chunk-section reader to assert world contents from wire bytes.
// Returns block state at (wx,wy,wz) from a LevelChunkWithLight body, or -1.
static std::int64_t chunkBlockAt(const std::vector<std::uint8_t>& body,
                                 std::int32_t wx, std::int32_t wy, std::int32_t wz) {
    ReadBuffer in(body);
    const std::int32_t cx = in.i32(), cz = in.i32();
    if (((wx >> 4) != cx) || ((wz >> 4) != cz)) return -1;
    { nbt::Reader r(in); r.skipRoot(); }                       // heightmaps
    const std::int32_t size = in.varint();
    const std::size_t end = in.off + static_cast<std::size_t>(size);
    std::vector<std::vector<std::uint32_t>> sections;
    while (in.off + 3 <= end && sections.size() < 24) {
        in.u16();                                            // block count (unused)
        // blocks container
        auto readContainer = [&]() -> std::vector<std::uint32_t> {
            const std::uint8_t bits = in.u8();
            if (bits == 0) {
                const std::int32_t v = in.varint();
                in.varint();                                    // longCount(=0)
                return std::vector<std::uint32_t>(4096, static_cast<std::uint32_t>(v));
            }
            const std::int32_t palN = in.varint();
            std::vector<std::uint32_t> pal(static_cast<std::size_t>(palN));
            for (auto& v : pal) v = static_cast<std::uint32_t>(in.varint());
            const std::int64_t nLongs = in.varint();
            const int per = 64 / bits;
            std::vector<std::uint32_t> vals; vals.reserve(4096);
            for (std::int64_t l = 0; l < nLongs && vals.size() < 4096; ++l) {
                const std::uint64_t word = in.u64();
                for (int e = 0; e < per && vals.size() < 4096; ++e)
                    vals.push_back(pal[(word >> (e * bits)) & ((1u << bits) - 1)]);
            }
            vals.resize(4096, pal.empty() ? 0 : pal.back());
            return vals;
        };
        sections.push_back(readContainer());
        readContainer();                                        // biomes
    }
    const int ly = wy + 64;
    if (ly < 0 || ly >= 384) return -1;
    if (sections.size() <= static_cast<std::size_t>(ly >> 4)) return -1;
    const auto& blocks = sections[static_cast<std::size_t>(ly >> 4)];
    if (blocks.size() != 4096) return -1;
    return blocks[((ly & 15) << 8) | ((wz & 15) << 4) | (wx & 15)];
}

// ------------------------------------------------------------------ scenarios
static void scenarioStatus(TestClient& c) {
    std::printf("[status]\n");
    const std::string js = c.queryStatusJson();
    CHECK(js.find("\"protocol\":769") != std::string::npos, "status advertises protocol 769");
    CHECK(js.find("1.21.4") != std::string::npos, "status advertises 1.21.4");
}

static void scenarioJoinBuildChat(ServerProc& srv) {
    std::printf("[join/build/chat/persistence]\n");
    TestClient a;
    CHECK(a.connect("127.0.0.1", srv.port), "client A connects");
    std::printf("    [diag] TesterA local port %u\n", a.localPort());
    CHECK(a.join("TesterA"), "A joins through login+configuration");
    Packet joinPkt;
    const bool gotJoin = a.waitFor([](const Packet& p){ return p.id == proto::pl::sc::Login; }, 5000, &joinPkt);
    if (!gotJoin) {
        std::string hist;
        std::lock_guard<std::mutex> lk(a.mtx_public());
        std::map<std::uint8_t,int> m;
        for (auto& p : a.recentPublic()) ++m[p.id];
        for (auto& [k,v] : m) hist += " " + std::to_string(k) + "x" + std::to_string(v);
        std::printf("    [diag] recent histogram: %s\n", hist.c_str());
    }
    CHECK(gotJoin, "A receives join game");

    // initial chunk flood around spawn (vd=2 -> at least 5x5)
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8000);
    while (a.chunkCoords.size() < 25 && std::chrono::steady_clock::now() < deadline) a.pump(50);
    CHECK(a.chunkCoords.size() >= 25, "A received initial chunks (>=25)");

    // declare_commands should have been advertised
    CHECK(a.declares >= 1, "A received command tree");

    // dig the grass under spawn column and verify echo + ack + persistence bytes
    a.sendPosition(0.5, -60.0, 0.5);
    a.pump(150);
    a.sendDig(0, -61, 0, 7);                    // break a surface grass block
    bool sawAck = false, sawAirEcho = false;
    const auto dl2 = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
    while (std::chrono::steady_clock::now() < dl2 && !(sawAck && sawAirEcho)) {
        a.pump(40);
        for (auto& u : a.blockUpdates)
            if (u.x == 0 && u.y == -61 && u.z == 0 && u.state == 0) sawAirEcho = true;
        sawAck = a.acks > 0;
    }
    CHECK(sawAck, "dig acknowledged (sequence)");
    CHECK(sawAirEcho, "block update broadcast for dug block");

    // chat round-trip
    a.sendChatMessage("integration-hello");
    bool sawChat = false;
    const auto dl3 = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
    while (std::chrono::steady_clock::now() < dl3 && !sawChat) {
        a.pump(40);
        for (auto& line : a.chatLines) if (line.find("integration-hello") != std::string::npos) sawChat = true;
    }
    CHECK(sawChat, "chat echoed back through system chat");

    // find chunk containing origin among A's raw chunks and verify air there now
    // (post-dig re-stream check happens on B below; A keeps its original chunks)

    a.close();

    // reconnect as B: edits must persist
    TestClient b;
    CHECK(b.connect("127.0.0.1", srv.port), "client B connects after A left");
    CHECK(b.join("TesterB"), "B joins");
    const auto dl4 = std::chrono::steady_clock::now() + std::chrono::milliseconds(8000);
    bool persisted = false;
    while (std::chrono::steady_clock::now() < dl4 && !persisted) {
        b.pump(50);
        for (auto& body : b.rawChunks) {
            ReadBuffer in(body);
            const std::int32_t cx = in.i32(), cz = in.i32();
            if (cx == 0 && cz == 0) persisted = chunkBlockAt(body, 0, -61, 0) == 0;
        }
    }
    CHECK(persisted, "edit persisted across reconnect (fresh chunk is air)");
    b.close();
}

static void scenarioMultiplayer(ServerProc& srv) {
    std::printf("[multiplayer visibility]\n");
    TestClient alice, bob;
    CHECK(alice.connect("127.0.0.1", srv.port) && alice.join("Alice"), "Alice joins");
    alice.pump(500);
    CHECK(bob.connect("127.0.0.1", srv.port) && bob.join("Bob"), "Bob joins");

    // Alice should receive Bob's spawn entity once he spawns
    bool aliceSeesBob = false, bobSeesAliceSpawn = false;
    const auto dl = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (std::chrono::steady_clock::now() < dl && !(aliceSeesBob && bobSeesAliceSpawn)) {
        alice.pump(30); bob.pump(30);
        aliceSeesBob = alice.spawnsReceived > 0;
        bobSeesAliceSpawn = bob.spawnsReceived > 0;
    }
    CHECK(aliceSeesBob, "Alice sees Bob's spawn_entity");
    CHECK(bobSeesAliceSpawn, "Bob sees Alice's spawn_entity");

    // movement relay
    alice.sendPosition(12.5, -60.0, 8.5);
    const auto dl2 = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
    while (bob.entityMoves == 0 && std::chrono::steady_clock::now() < dl2) { alice.pump(20); bob.pump(20); }
    CHECK(bob.entityMoves > 0, "Bob sees Alice move");

    // chat cross-delivery + leave broadcast
    alice.sendChatMessage("hi-bob");
    bool bobGotChat = false;
    const auto dl3 = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
    while (!bobGotChat && std::chrono::steady_clock::now() < dl3) {
        bob.pump(40);
        for (auto& l : bob.chatLines) if (l.find("hi-bob") != std::string::npos) bobGotChat = true;
    }
    CHECK(bobGotChat, "Bob receives Alice's chat");
    alice.close();
    bool bobSawLeave = false;
    const auto dl4 = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
    while (!bobSawLeave && std::chrono::steady_clock::now() < dl4) {
        bob.pump(40);
        for (auto& l : bob.chatLines) if (l.find("left the game") != std::string::npos) bobSawLeave = true;
    }
    CHECK(bobSawLeave, "Bob sees leave broadcast");
    bob.close();
}

static void scenarioStress(ServerProc& srv, int n) {
    std::printf("[stress x%d]\n", n);
    std::atomic<int> ok{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < n; ++i)
        threads.emplace_back([&, i]{
            TestClient t;
            if (!t.connect("127.0.0.1", srv.port)) return;
            if (!t.join("Bot" + std::to_string(i))) return;
            const auto dl = std::chrono::steady_clock::now() + std::chrono::milliseconds(15000);
            while (t.chunkCoords.size() < 3 && std::chrono::steady_clock::now() < dl) t.pump(50);
            if (t.count(proto::pl::sc::Login) > 0 && !t.chunkCoords.empty()) ++ok;
            t.close();
        });
    for (auto& th : threads) th.join();
    CHECK(ok == n, "all stress bots joined with chunks");
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* serverPath = argc > 1 ? argv[1] : "build/cppfm";
    std::printf("=== cppfm native self-test (server: %s) ===\n", serverPath);

    ServerProc srv;
    if (!srv.start(serverPath, 2)) {
        std::printf("FATAL: could not start server\n");
        return 2;
    }

    {
        TestClient c;
        c.connect("127.0.0.1", srv.port);
        scenarioStatus(c);
        c.close();
    }
    scenarioJoinBuildChat(srv);
    scenarioMultiplayer(srv);
    scenarioStress(srv, 12);

    srv.stop();
    std::printf("\n%s (%d failures)\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
