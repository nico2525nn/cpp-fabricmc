// test_flood_net.cpp — plan46 §1 (O-13/W-14/W-16) flood/defense tests, A1–A8.
// Unit part: RateLimiter / SpamTracker / AcceptGate / PacketDecoder budgets /
//   decompressChecked (no server). Live part: forks cppfm (argv[1]) and drives
//   real sockets + TestClient through the production net path.

#include "TestClient.hpp"
#include "../src/core/ByteBuffer.hpp"
#include "../src/core/Zlib.hpp"
#include "../src/net/PacketDecoder.hpp"
#include "../src/net/PacketEncoder.hpp"
#include "../src/net/RateLimiter.hpp"
#include "../src/proto/Ids.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace cppfm;
using namespace cpptest;

#include "Harness.hpp"

// ---- unit: RateLimiter (A1) ------------------------------------------------
static void unitRateLimiter() {
    SECTION("U-A1 RateLimiter token bucket");
    RateLimiter rl(100.0, 50.0, 1000);
    CHECK(rl.consume(60.0, 1000), "burst 60/100 ok");
    CHECK(rl.consume(40.0, 1000), "burst +40/100 ok");
    CHECK(!rl.consume(1.0, 1000), "over burst refused");
    CHECK(rl.consume(50.0, 2000), "1s refill gives 50");
    CHECK(!rl.consume(1.0, 2000), "refill exact, nothing left");
    CHECK(rl.consume(25.0, 2500), "0.5s refill gives 25");
    // default production budget: 2MB burst / 1MB/s
    RateLimiter bw;
    CHECK(bw.consume(2 * 1024 * 1024, 5000), "2MB single frame fits burst");
    CHECK(!bw.consume(8 * 1024 * 1024, 5000), "8MB frame exceeds burst");
}

// ---- unit: SpamTracker (A3) -------------------------------------------------
static void unitSpam() {
    SECTION("U-A3 SpamTracker vanilla 200式");
    SpamTracker s;
    bool kicked = false;
    for (int i = 0; i < 10; ++i) kicked = s.onChat(100);
    CHECK(!kicked, "10 msg same tick: count=200, no kick");
    CHECK(s.onChat(100), "11th msg same tick: 220>200 kick");
    SpamTracker s2;
    for (int i = 0; i < 5; ++i) (void)s2.onChat(0);
    CHECK(!s2.onChat(500), "500 ticks decay clears 100 -> +20 no kick");
    SpamTracker s3;
    (void)s3.onChat(0);
    CHECK(!s3.onChat(7), "7 ticks decay 20->13 -> +20=33 no kick");
}

// ---- unit: AcceptGate (A5) --------------------------------------------------
static void unitAcceptGate() {
    SECTION("U-A5 AcceptGate 20/s");
    AcceptGate g(20);
    bool ok = true;
    for (int i = 0; i < 20; ++i) ok = ok && g.allow(1000);
    CHECK(ok, "first 20 allowed in window");
    CHECK(!g.allow(1000), "21st refused in same window");
    CHECK(!g.allow(1999), "still refused at window end");
    CHECK(g.allow(2000), "new window allows again");
}

// ---- unit: PacketDecoder budgets (A2/A7/A8) ---------------------------------
static std::vector<std::uint8_t> varintVec(std::int32_t v) {
    std::vector<std::uint8_t> o;
    WriteBuffer::writeVarintTo(o, v);
    return o;
}

static void unitDecoder() {
    SECTION("U-A2 declared 2MB budget (kick before alloc)");
    {
        // dataLen = 2MB+1 with only 10 junk bytes: must throw OversizeError
        // WITHOUT allocating 2MB (check precedes inflate).
        auto f = varintVec(static_cast<std::int32_t>(2 * 1024 * 1024 + 1));
        f.insert(f.end(), 10, 0x00);
        bool over = false;
        try { (void)PacketDecoder::decodeFrame(f, 256); }
        catch (const PacketDecoder::OversizeError&) { over = true; }
        catch (...) {}
        CHECK(over, "declared 2MB+1 -> OversizeError");
    }
    {
        // declared 8MB (zlib-bomb shape): OversizeError, not bad_alloc.
        auto f = varintVec(static_cast<std::int32_t>(8 * 1024 * 1024));
        f.insert(f.end(), 4, 0x78);
        bool over = false;
        try { (void)PacketDecoder::decodeFrame(f, 256); }
        catch (const PacketDecoder::OversizeError&) { over = true; }
        catch (...) {}
        CHECK(over, "declared 8MB bomb -> OversizeError");
    }
    {
        // negative declaration rejected.
        auto f = varintVec(-1);
        f.insert(f.end(), 4, 0x00);
        bool threw = false;
        try { (void)PacketDecoder::decodeFrame(f, 256); }
        catch (...) { threw = true; }
        CHECK(threw, "negative dataLength rejected");
    }

    SECTION("U-A7 dataLength forgery (below threshold)");
    {
        // threshold=256 but dataLen=100: no conforming encoder emits this.
        auto f = varintVec(100);
        f.insert(f.end(), 8, 0x11);
        bool threw = false;
        try { (void)PacketDecoder::decodeFrame(f, 256); }
        catch (...) { threw = true; }
        CHECK(threw, "dataLen=100 < threshold=256 rejected");
    }
    {
        // trailing garbage after a valid stream is rejected (W-14(b)).
        // NOTE: body must be >= threshold so the frame itself is legal.
        std::vector<std::uint8_t> big(300, 0x41);
        std::vector<std::uint8_t> comp;
        compressRaw(big.data(), big.size(), comp);
        auto f = varintVec(300);
        f.insert(f.end(), comp.begin(), comp.end());
        f.push_back(0x00); // trailing junk
        bool threw = false;
        try { (void)PacketDecoder::decodeFrame(f, 256); }
        catch (...) { threw = true; }
        CHECK(threw, "trailing garbage rejected");
        // ...but the clean stream decodes.
        f.pop_back();
        bool ok = false;
        try {
            auto body = PacketDecoder::decodeFrame(f, 256);
            ok = (body == big);
        } catch (...) {}
        CHECK(ok, "clean stream still decodes");
    }

    SECTION("U-W14 boundary 255/256/257 + threshold=0 (A8)");
    for (int total : {255, 256, 257}) {
        std::vector<std::uint8_t> idp;
        idp.push_back(0x07);
        idp.insert(idp.end(), static_cast<std::size_t>(total - 1), 0x41);
        auto outer = PacketEncoder::encode(idp, 256);
        // strip outer length varint -> frame
        ReadBuffer ro(outer);
        (void)ro.varint();
        std::vector<std::uint8_t> frame(ro.p + ro.off, ro.p + ro.len);
        bool ok = false;
        try {
            auto body = PacketDecoder::decodeFrame(frame, 256);
            ok = (body == idp);
        } catch (...) {}
        char m[96];
        std::snprintf(m, sizeof(m), "roundtrip total=%d threshold=256", total);
        CHECK(ok, m);
    }
    {
        // threshold=0: everything compressed, even tiny bodies.
        std::vector<std::uint8_t> idp{0x07, 0x41, 0x42};
        auto outer = PacketEncoder::encode(idp, 0);
        ReadBuffer ro(outer);
        (void)ro.varint();
        std::vector<std::uint8_t> frame(ro.p + ro.off, ro.p + ro.len);
        bool ok = false;
        try {
            auto body = PacketDecoder::decodeFrame(frame, 0);
            ok = (body == idp);
        } catch (...) {}
        CHECK(ok, "threshold=0 small packet compressed roundtrip");
        // threshold=0 with an uncompressed (dataLength=0) frame: invalid.
        std::vector<std::uint8_t> raw{0x00, 0x07, 0x41};
        bool threw = false;
        try { (void)PacketDecoder::decodeFrame(raw, 0); }
        catch (...) { threw = true; }
        CHECK(threw, "threshold=0 rejects dataLength=0 frame");
    }
}

// ---- unit: SO_RCVTIMEO slow-loris guard (A6 mechanism) ------------------------
static void unitRecvTimeout() {
    SECTION("U-A6 readExact recv timeout (production path)");
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
    if (sv[0] < 0) return;
    Connection c(sv[0]);
    c.setRecvTimeout(1);
    auto t0 = std::chrono::steady_clock::now();
    bool timedOut = false;
    try { (void)c.readFrame(); }
    catch (const SocketClosedError& e) { timedOut = e.timedOut; }
    catch (...) {}
    auto dt = std::chrono::steady_clock::now() - t0;
    CHECK(timedOut, "idle read fails with timedOut=true");
    CHECK(dt < std::chrono::seconds(10), "timeout fires promptly (~1s, not forever)");
    // normal frame still passes with the timeout armed.
    std::vector<std::uint8_t> body{0x07, 0x41};
    std::vector<std::uint8_t> outer;
    WriteBuffer::writeVarintTo(outer, static_cast<std::int32_t>(body.size()));
    outer.insert(outer.end(), body.begin(), body.end());
    CHECK(::send(sv[1], outer.data(), outer.size(), MSG_NOSIGNAL) > 0, "inject frame");
    bool ok = false;
    try { ok = (c.readFrame() == body); } catch (...) {}
    CHECK(ok, "framed read works with timeout armed");
    ::close(sv[1]);
}

// ---- live harness ------------------------------------------------------------
struct ServerProc {
    pid_t pid = -1;
    std::uint16_t port = 0;
    std::string worldDir;
    bool start(const char* bin) {
        port = static_cast<std::uint16_t>(26100 + (getpid() % 2500));
        worldDir = "/tmp/floodnet-" + std::to_string(getpid());
        std::filesystem::remove_all(worldDir);
        std::filesystem::create_directories(worldDir);
        for (int a = 0; a < 20; ++a) {
            int s = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in ad{};
            ad.sin_family = AF_INET;
            ad.sin_port = htons(port);
            ad.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (::connect(s, reinterpret_cast<sockaddr*>(&ad), sizeof(ad)) != 0) break;
            ::close(s);
            ++port;
        }
        pid = fork();
        if (pid == 0) {
            char pa[32], va[32], wa[256];
            std::snprintf(pa, sizeof(pa), "--port=%u", port);
            std::snprintf(va, sizeof(va), "--view-distance=%d", 4);
            std::snprintf(wa, sizeof(wa), "--world-dir=%s", worldDir.c_str());
            execl(bin, bin, pa, va, wa, "--online-mode=false", (char*)nullptr);
            _exit(127);
        }
        for (int i = 0; i < 80; ++i) {
            int s = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in ad{};
            ad.sin_family = AF_INET;
            ad.sin_port = htons(port);
            ad.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            bool up = (::connect(s, reinterpret_cast<sockaddr*>(&ad), sizeof(ad)) == 0);
            ::close(s);
            if (up) return true;
            usleep(100 * 1000);
        }
        return false;
    }
    void stop() {
        if (pid > 0) {
            kill(pid, SIGTERM);
            int st = 0;
            for (int i = 0; i < 25; ++i) {
                pid_t r = waitpid(pid, &st, WNOHANG);
                if (r == pid || r == -1) break;
                usleep(100 * 1000);
            }
            if (kill(pid, 0) == 0) { kill(pid, SIGKILL); waitpid(pid, &st, 0); }
            else if (pid > 0) waitpid(pid, &st, WNOHANG);
            pid = -1;
            std::filesystem::remove_all(worldDir);
        }
    }
};

static int rawConnect(std::uint16_t port) {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    timeval tv{5, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    sockaddr_in ad{};
    ad.sin_family = AF_INET;
    ad.sin_port = htons(port);
    ad.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(s, reinterpret_cast<sockaddr*>(&ad), sizeof(ad)) != 0) {
        ::close(s);
        return -1;
    }
    return s;
}

static void sendVarint(int fd, std::int32_t v) {
    std::uint8_t buf[5];
    int n = 0;
    std::uint32_t u = static_cast<std::uint32_t>(v);
    do {
        std::uint8_t b = u & 0x7F;
        u >>= 7;
        if (u) b |= 0x80;
        buf[n++] = b;
    } while (u);
    (void)::send(fd, buf, static_cast<std::size_t>(n), MSG_NOSIGNAL);
}

static void sendHandshake(int fd, int nextState, std::uint16_t port) {
    WriteBuffer b;
    b.varint(769);
    b.string("127.0.0.1");
    // port u16 big-endian
    b.data.push_back(static_cast<std::uint8_t>(port >> 8));
    b.data.push_back(static_cast<std::uint8_t>(port & 0xFF));
    b.varint(nextState);
    std::vector<std::uint8_t> outer;
    WriteBuffer::writeVarintTo(outer, static_cast<std::int32_t>(1 + b.data.size()));
    outer.push_back(0x00); // handshake pid
    outer.insert(outer.end(), b.data.begin(), b.data.end());
    (void)::send(fd, outer.data(), outer.size(), MSG_NOSIGNAL);
}

// read one framed packet id (uncompressed path); -1 on closed/timeout.
static int readFrameId(int fd) {
    std::uint32_t len = 0;
    int shift = 0;
    for (int i = 0; i < 5; ++i) {
        std::uint8_t by = 0;
        ssize_t r = ::recv(fd, &by, 1, 0);
        if (r != 1) return -1;
        len |= static_cast<std::uint32_t>(by & 0x7F) << shift;
        if (!(by & 0x80)) break;
        shift += 7;
    }
    if (len == 0 || len > 16 * 1024 * 1024) return -2;
    std::vector<std::uint8_t> buf(len);
    std::size_t off = 0;
    while (off < len) {
        ssize_t r = ::recv(fd, buf.data() + off, len - off, 0);
        if (r <= 0) return -1;
        off += static_cast<std::size_t>(r);
    }
    // pid varint (single byte for small ids)
    return static_cast<int>(buf[0]);
}

static bool statusAlive(std::uint16_t port) {
    int s = rawConnect(port);
    if (s < 0) return false;
    sendHandshake(s, 1, port);
    std::vector<std::uint8_t> req{0x01, 0x00}; // len=1, status request pid 0
    (void)::send(s, req.data(), req.size(), MSG_NOSIGNAL);
    int id = readFrameId(s);
    ::close(s);
    return id == 0x00; // status response
}

static void liveTests(const char* bin) {
    ServerProc srv;
    CHECK(srv.start(bin), "server boot for flood tests");
    if (srv.pid <= 0) return;
    CHECK(statusAlive(srv.port), "baseline status alive");

    SECTION("L-A1 8MB frame burst refused, server survives");
    {
        int s = rawConnect(srv.port);
        CHECK(s >= 0, "connect for A1");
        if (s >= 0) {
            sendHandshake(s, 2, srv.port);
            sendVarint(s, 8 * 1024 * 1024); // length only: budget kills pre-alloc
            // login-stage oversize => LoginDisconnect 0x00, or plain close.
            int id = readFrameId(s);
            CHECK(id == 0x00 || id == -1, "oversize frame -> kick (LoginDisconnect or close)");
            ::close(s);
        }
        CHECK(statusAlive(srv.port), "server alive after A1");
    }

    SECTION("L-A2 3MB declared frame refused, server survives");
    {
        int s = rawConnect(srv.port);
        CHECK(s >= 0, "connect for A2");
        if (s >= 0) {
            sendHandshake(s, 2, srv.port);
            sendVarint(s, 3 * 1024 * 1024);
            int id = readFrameId(s);
            CHECK(id == 0x00 || id == -1, "3MB frame -> kick (LoginDisconnect or close)");
            ::close(s);
        }
        CHECK(statusAlive(srv.port), "server alive after A2");
    }

    SECTION("L-A2b 3MB play payload -> Disconnect kick, server survives");
    {
        TestClient c;
        CHECK(c.connect("127.0.0.1", srv.port) && c.join("FloodA2"), "join A2b");
        if (c.alive()) {
            // incompressible 3MB custom payload -> frame ~3MB -> budget kick.
            std::mt19937 rng(1234);
            WriteBuffer b;
            b.string("minecraft:brand");
            for (int i = 0; i < 3 * 1024 * 1024; ++i)
                b.data.push_back(static_cast<std::uint8_t>(rng()));
            c.sendRawPlay(proto::pl::cs::CustomPayload, b);
            Packet got;
            bool sawDisc = c.waitFor(
                [](const Packet& p) { return p.id == proto::pl::sc::Disconnect; },
                5000, &got);
            CHECK(sawDisc, "oversize play payload -> Disconnect 0x1D");
        }
        c.close();
        CHECK(statusAlive(srv.port), "server alive after A2b");
    }

    SECTION("L-A3 chat 20msg/s flood -> disconnect.spam kick, others unaffected");
    {
        TestClient spammer;
        CHECK(spammer.connect("127.0.0.1", srv.port) && spammer.join("Spammer"), "join spammer");
        TestClient normal;
        CHECK(normal.connect("127.0.0.1", srv.port) && normal.join("Normal"), "join normal");
        if (spammer.alive() && normal.alive()) {
            for (int i = 0; i < 15; ++i)
                spammer.sendChatMessage("spam-" + std::to_string(i));
            Packet got;
            bool kicked = spammer.waitFor(
                [](const Packet& p) { return p.id == proto::pl::sc::Disconnect; },
                6000, &got);
            CHECK(kicked, "spammer kicked with Disconnect");
            // normal client unaffected: own chat echoes back.
            normal.sendChatMessage("normal-after-flood");
            bool echo = false;
            auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            while (std::chrono::steady_clock::now() < dl) {
                normal.pump(100);
                for (auto& l : normal.chatLines)
                    if (l.find("normal-after-flood") != std::string::npos) echo = true;
                if (echo) break;
            }
            CHECK(echo, "normal client chat unaffected");
        }
        spammer.close();
        normal.close();
        CHECK(statusAlive(srv.port), "server alive after A3");
    }

    SECTION("L-A4 malformed x200 (unknown pid) -> session survives");
    {
        TestClient c;
        CHECK(c.connect("127.0.0.1", srv.port) && c.join("Malformed"), "join A4");
        if (c.alive()) {
            WriteBuffer junk;
            junk.data = {0xDE, 0xAD, 0xBE, 0xEF};
            for (int i = 0; i < 200; ++i) c.sendRawPlay(0x7F, junk);
            // truncated chat (parse-throw path) x5 — under the spam budget.
            WriteBuffer trunc;
            trunc.data = {0xFF, 0xFF};
            for (int i = 0; i < 5; ++i)
                c.sendRawPlay(proto::pl::cs::ChatMessage, trunc);
            c.sendChatMessage("alive-after-malformed");
            bool echo = false;
            auto dl = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            while (std::chrono::steady_clock::now() < dl) {
                c.pump(100);
                for (auto& l : c.chatLines)
                    if (l.find("alive-after-malformed") != std::string::npos) echo = true;
                if (echo) break;
            }
            CHECK(c.alive(), "session still alive after 205 malformed");
            CHECK(echo, "valid chat after malformed burst echoes");
        }
        c.close();
        CHECK(statusAlive(srv.port), "server alive after A4");
    }

    SECTION("L-A5 connection burst -> server survives");
    {
        int answered = 0;
        for (int i = 0; i < 40; ++i) {
            if (statusAlive(srv.port)) ++answered;
        }
        CHECK(answered > 0, "burst: status queries answered");
        std::printf("    (answered %d/40 — gate may refuse, survival is the assert)\n", answered);
        // the burst saturates the 1s accept window: reset before probing.
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        CHECK(statusAlive(srv.port), "server alive after A5 burst");
        // let the 1s accept window reset so A6 starts unsaturated.
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    }

    SECTION("L-A6 idle connection not cut prematurely");
    {
        int s = rawConnect(srv.port);
        CHECK(s >= 0, "connect for A6");
        if (s >= 0) {
            sendHandshake(s, 1, srv.port);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            // half-open but handshaked: socket should still be usable.
            std::vector<std::uint8_t> req{0x01, 0x00};
            bool sent = (::send(s, req.data(), req.size(), MSG_NOSIGNAL) == 2);
            int id = sent ? readFrameId(s) : -99;
            CHECK(id == 0x00, "idle-2s connection still served");
            ::close(s);
        }
        CHECK(statusAlive(srv.port), "server alive after A6");
    }

    srv.stop();
}

int main(int argc, char** argv) {
    std::printf("=== test_flood_net — plan46 §1 A1–A8 ===\n");
    unitRateLimiter();
    unitSpam();
    unitAcceptGate();
    unitDecoder();
    unitRecvTimeout();
    if (argc > 1) {
        liveTests(argv[1]);
    } else {
        std::printf("\n(live tests skipped: no server binary arg)\n");
    }
    std::printf("\nRESULT flood_net: %d PASS %d FAIL\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
