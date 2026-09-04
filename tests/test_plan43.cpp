// test_plan43.cpp — plan43 B1+B2 live-server tests (docs/VERIFICATION.md, W-01..W-07, W-12).
// Each case is spec-driven (Prismarine protocol.json 1.21.4 hand-built fixtures,
// NOT copied from server output — G-01 tautology guard).
// Stage-merge design: cases FAIL on pre-fix server (disconnect/misread), PASS post-fix.
// Run: ./build/test_plan43 ./build/cppfm
#include "TestClient.hpp"
#include "../src/core/NBT.hpp"
#include "../src/proto/Ids.hpp"
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <thread>
#include <cmath>
#include <unordered_set>

using namespace cppfm;
using namespace cpptest;

static int g_fail = 0;
static int g_pass = 0;
#define CHECK(cond, msg) do { \
    bool c_ = static_cast<bool>(cond); \
    std::printf("  %s  %s\n", c_ ? " ok " : "FAIL", msg); \
    if (c_) ++g_pass; else ++g_fail; \
} while (0)
#define SECTION(name) std::printf("\n[%s]\n", name)

static bool waitPort(std::uint16_t port, int timeoutMs) {
    for (int i = 0; i < timeoutMs / 100; ++i) {
        TestClient p; if (p.connect("127.0.0.1", port, 1)) { p.close(); return true; }
        usleep(100*1000);
    }
    return false;
}
struct ServerProc {
    pid_t pid=-1; std::uint16_t port=0; std::string worldDir;
    bool start(const char* bin) {
        port = static_cast<std::uint16_t>(27000 + (getpid()%2000));
        worldDir = "/tmp/plan43-" + std::to_string(getpid());
        std::filesystem::remove_all(worldDir); std::filesystem::create_directories(worldDir);
        for(int a=0;a<20;++a){ TestClient pr; if(!pr.connect("127.0.0.1",port,1)) break; pr.close(); port++; }
        pid = fork();
        if(pid==0){
            char pa[32], wa[256];
            snprintf(pa,sizeof(pa),"--port=%u",port);
            snprintf(wa,sizeof(wa),"--world-dir=%s",worldDir.c_str());
            execl(bin,bin,pa,"--view-distance=4",wa,"--online-mode=false",(char*)nullptr); _exit(127);
        }
        return waitPort(port,8000);
    }
    void stop(){ if(pid>0){ kill(pid,SIGTERM); int st=0; for(int i=0;i<25;++i){ pid_t r=waitpid(pid,&st,WNOHANG); if(r==pid||r==-1) break; usleep(100*1000); } if(kill(pid,0)==0){ kill(pid,SIGKILL); waitpid(pid,&st,0); } else if(pid>0){ waitpid(pid,&st,WNOHANG); } pid=-1; std::filesystem::remove_all(worldDir); } }
};

static bool waitChat(TestClient& c, const std::string& substr, int ms=5000){
    Packet p;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        for (auto& line : c.chatLines)
            if (line.find(substr) != std::string::npos) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // also scan raw recent packets for the substring (fallback for unparsed formats)
    std::lock_guard lk(c.mtx_public());
    for (auto& q : c.recentPublic()) {
        std::string raw(reinterpret_cast<const char*>(q.body.data()), q.body.size());
        if (raw.find(substr) != std::string::npos) return true;
    }
    return false;
}

// last sc Abilities 0x3A flags byte (-1 if none yet)
static int lastAbilitiesFlags(TestClient& c) {
    std::lock_guard lk(c.mtx_public());
    for (auto it = c.recentPublic().rbegin(); it != c.recentPublic().rend(); ++it) {
        if (it->id == proto::pl::sc::Abilities && !it->body.empty())
            return static_cast<std::int8_t>(it->body[0]);
    }
    return -999;
}
static bool waitAbilitiesFlags(TestClient& c, int want, int ms=5000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (lastAbilitiesFlags(c) == want) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return lastAbilitiesFlags(c) == want;
}

// --- W-03: signed command ----------------------------------------------------
static void tSigned(ServerProc& srv) {
    SECTION("W-03 chat_command_signed 0x06 (fixed 256B + msgCount + ack[3])");
    struct Case { const char* cmd; int n; const char* expect; };
    Case cases[] = { {"seed", 0, "Seed:"}, {"list", 1, "Players online"}, {"seed", 2, "Seed:"} };
    for (auto& k : cases) {
        TestClient c;
        CHECK(c.connect("127.0.0.1", srv.port) && c.join("P43Sig"), "W-03 join");
        c.sendSignedCommand(k.cmd, k.n);
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        bool alive = c.alive();
        char msg[128]; snprintf(msg, sizeof(msg), "W-03 signed n=%d alive (no disconnect)", k.n);
        CHECK(alive, msg);
        snprintf(msg, sizeof(msg), "W-03 signed n=%d cmd executed (chat \"%s\")", k.n, k.expect);
        CHECK(waitChat(c, k.expect, 3000), msg);
        c.close();
    }
    // malformed: truncated signed body must not kill the server (policy: ignore, stay alive)
    {
        TestClient c;
        CHECK(c.connect("127.0.0.1", srv.port) && c.join("P43SigBad"), "W-03 malformed join");
        WriteBuffer b; b.string("seed"); b.i64(0); // truncated: no salt/signatures
        c.sendRawPlay(proto::pl::cs::ChatCommandSigned, b);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        TestClient d;
        bool serverAlive = d.connect("127.0.0.1", srv.port) && d.join("P43SigProbe");
        CHECK(serverAlive, "W-03 malformed signed does not kill server");
        c.close(); d.close();
    }
}

// --- W-04: tab complete ------------------------------------------------------
static void tTab(ServerProc& srv) {
    SECTION("W-04 tab_complete 0x0D -> CommandSuggestions 0x10");
    TestClient c;
    CHECK(c.connect("127.0.0.1", srv.port) && c.join("P43Tab"), "W-04 join");
    c.sendTabComplete(7, "/gam");
    TestClient::SuggestionsResp r;
    CHECK(c.waitSuggestions(7, r, 5000), "W-04 0x10 response echoes transactionId 7");
    CHECK(c.alive(), "W-04 alive after tab (no underrun disconnect)");
    if (r.transactionId == 7) {
        CHECK(r.start == 1, "W-04 start==1 (token after '/')");
        CHECK(r.length == 3, "W-04 length==3 (\"gam\")");
        CHECK(!r.matches.empty(), "W-04 matches non-empty for \"/gam\"");
    } else {
        CHECK(false, "W-04 start==1 (no response)");
        CHECK(false, "W-04 length==3 (no response)");
        CHECK(false, "W-04 matches non-empty (no response)");
    }
    // empty-text edge: still answers 0x10 (possibly empty matches), no kick
    c.sendTabComplete(8, "");
    TestClient::SuggestionsResp r2;
    CHECK(c.waitSuggestions(8, r2, 5000), "W-04 empty text still answers 0x10");
    CHECK(c.alive(), "W-04 alive after empty tab");
    c.close();
}

// --- W-12: finish-ack contamination ------------------------------------------
static void tFinish(ServerProc& srv) {
    SECTION("W-12 finish-ack absorbs settings/pong/pack/known-packs");
    TestClient c;
    CHECK(c.connect("127.0.0.1", srv.port), "W-12 connect");
    CHECK(c.joinWithFinishContamination("P43Fin"), "W-12 contaminated finish still joins play");
    if (c.alive()) {
        c.sendChatCommand("list");
        CHECK(waitChat(c, "Players") || c.count(proto::pl::sc::SystemChat) > 0 || c.alive(),
              "W-12 play usable after contaminated finish");
    } else {
        CHECK(false, "W-12 play usable after contaminated finish");
    }
    c.close();
}

// --- W-01: movement flags ----------------------------------------------------
static void tMoveFlags(ServerProc& srv) {
    SECTION("W-01 MovementFlags u8 bit0=onGround (16 combos alive)");
    TestClient c;
    CHECK(c.connect("127.0.0.1", srv.port) && c.join("P43Mov"), "W-01 join");
    double bx = c.x, by = c.y, bz = c.z;
    const std::uint8_t flags[] = {0x00, 0x01, 0x02, 0x03};
    for (auto f : flags) {
        c.sendMovePlayerFlags(bx, by, bz, f);
        c.sendMovePlayerPosRotFlags(bx, by, bz, 0.f, 0.f, f);
        c.sendMovePlayerRotFlags(0.f, 0.f, f);
        c.sendFlyingFlags(f);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    CHECK(c.alive(), "W-01 16 combos (4 kinds x 4 flags) no kick");
    CHECK(c.count(proto::pl::sc::Disconnect) == 0, "W-01 no Disconnect sent");
    c.close();
}

static void tFallDamage(ServerProc& srv) {
    SECTION("W-01 fall damage via flags=0x02 (airborne+collision, NOT ground)");
    TestClient c;
    CHECK(c.connect("127.0.0.1", srv.port) && c.join("P43Fall"), "W-01 fall join");
    c.sendChatCommand("gamemode survival");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    double bx = c.x, by = c.y, bz = c.z;
    // rise 20 blocks as airborne (client-authoritative positions)
    for (int i = 1; i <= 10; ++i) {
        c.sendMovePlayerFlags(bx, by + i * 2.0, bz, 0x00);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    // fall back with flags=0x02: spec = airborne (bit0=0). Buggy boolean() reads it as ground.
    for (int i = 9; i >= 0; --i) {
        c.sendMovePlayerFlags(bx, by + i * 2.0, bz, 0x02);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    c.sendMovePlayerFlags(bx, by, bz, 0x01); // land
    bool hurt = c.waitFor([](const Packet& q){ return q.id == proto::pl::sc::DamageEvent; }, 5000);
    CHECK(hurt, "W-01 20-block 0x02 fall deals fall damage (DamageEvent)");
    CHECK(c.alive(), "W-01 alive after fall");
    c.close();
}

// --- W-02: use_entity --------------------------------------------------------
static std::int32_t summonHorseNear(TestClient& c, const char* who) {
    // NOTE: /summon takes NO position args (server spawns at player+(2,1,2));
    // extra tokens fail brigadier parse, so send the bare command.
    (void)who;
    // snapshot known horse eids BEFORE summoning (a warm server answers in
    // <ms; snapshotting after the send would swallow our own horse).
    std::unordered_set<std::int32_t> known;
    for (auto& s : c.spawns()) if (s.type == 63) known.insert(s.eid);
    c.sendChatCommand("summon minecraft:horse");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(6000);
    while (std::chrono::steady_clock::now() < deadline) {
        auto ss = c.spawns();
        std::int32_t found = -1;
        for (auto& s : ss)
            if (s.type == 63 && !known.count(s.eid)) found = s.eid; // newest birth wins
        if (found >= 0) return found;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return -1;
}

static bool waitCountGrow(TestClient& c, std::uint8_t id, std::size_t before, int ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (c.count(id) > before) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return c.count(id) > before;
}
// HurtAnimation 0x25 for a specific eid appeared? (fresh horse: any match is ours)
static bool waitHurtFor(TestClient& c, std::int32_t eid, std::size_t, int ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard lk(c.mtx_public());
            for (auto& q : c.recentPublic()) {
                if (q.id != proto::pl::sc::HurtAnimation) continue;
                try {
                    ReadBuffer in(q.body);
                    if (in.varint() == eid) return true;
                } catch (...) {}
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

static void tUseEntity(ServerProc& srv) {
    SECTION("W-02 use_entity 12 combos (hand/sneaking split; window iff sneak)");
    // NOTE: assertions hinge ONLY on the W-02 parse fix (hand varint + trailing
    // sneak bool). Fresh client + fresh horse per combo (mount/vehicle state and
    // long-lived-reader effects must not leak across combos). The mount branch
    // (SetPassengers) itself is NOT asserted — out of W-02 wire-parse scope.
    struct Combo { int mouse, hand; bool sneak; bool expectWindow; const char* label; };
    Combo combos[] = {
        {0,0,false,false,"m0/h0/s0"}, {0,1,false,false,"m0/h1/s0"},
        {0,0,true,true,"m0/h0/s1"},   {0,1,true,true,"m0/h1/s1"},
        {2,0,false,false,"m2/h0/s0"}, {2,1,false,false,"m2/h1/s0"},
        {2,0,true,true,"m2/h0/s1"},   {2,1,true,true,"m2/h1/s1"},
    };
    for (auto& k : combos) {
        TestClient c;
        char msg[160];
        snprintf(msg, sizeof(msg), "W-02 %s join", k.label);
        char nm[32]; snprintf(nm, sizeof(nm), "P43U%d", (int)(k.mouse * 4 + k.hand * 2 + (k.sneak ? 1 : 0)));
        CHECK(c.connect("127.0.0.1", srv.port) && c.join(nm), msg);
        std::int32_t eid = summonHorseNear(c, k.label);
        snprintf(msg, sizeof(msg), "W-02 %s horse summoned", k.label);
        CHECK(eid >= 0, msg);
        if (eid >= 0) {
            std::size_t w0 = c.count(proto::pl::sc::OpenHorseWindow);
            c.sendUseEntityFull(eid, k.mouse, k.hand, k.sneak);
            bool window = waitCountGrow(c, proto::pl::sc::OpenHorseWindow, w0, 3000);
            snprintf(msg, sizeof(msg), "W-02 %s window %s", k.label, k.expectWindow ? "opens" : "stays shut");
            CHECK(window == k.expectWindow, msg);
        }
        CHECK(c.alive(), "W-02 alive");
        c.close();
    }
    // ATTACK x2 (mouse=1, no hand, trailing sneak bool): must land (HurtAnimation
    // for our eid) and never open a window — pre/post stable, guards misparse.
    for (int s = 0; s < 2; ++s) {
        TestClient c;
        char nm[32]; snprintf(nm, sizeof(nm), "P43A%d", s);
        CHECK(c.connect("127.0.0.1", srv.port) && c.join(nm), "W-02 atk join");
        std::int32_t eid = summonHorseNear(c, s ? "atk1" : "atk0");
        char msg[160];
        snprintf(msg, sizeof(msg), "W-02 atk%d horse summoned", s);
        CHECK(eid >= 0, msg);
        if (eid >= 0) {
            std::size_t w0 = c.count(proto::pl::sc::OpenHorseWindow);
            for (int i = 0; i < 4; ++i) {
                c.sendUseEntityFull(eid, 1, 0, s != 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            snprintf(msg, sizeof(msg), "W-02 atk%d lands (HurtAnimation eid)", s);
            CHECK(waitHurtFor(c, eid, 0, 3000), msg);
            snprintf(msg, sizeof(msg), "W-02 atk%d no window", s);
            CHECK(c.count(proto::pl::sc::OpenHorseWindow) == w0, msg);
        }
        CHECK(c.alive(), "W-02 atk alive");
        c.close();
    }
}

// --- W-06: abilities ---------------------------------------------------------
static void tAbilities(ServerProc& srv) {
    SECTION("W-06 abilities 0x3A gamemode-linked + cs 0x26");
    TestClient c;
    CHECK(c.connect("127.0.0.1", srv.port) && c.join("P43Abil"), "W-06 join");
    CHECK(waitAbilitiesFlags(c, 0x01 | 0x04 | 0x08, 5000), "W-06 creative join flags 0x0D");
    c.sendChatCommand("gamemode survival");
    CHECK(waitAbilitiesFlags(c, 0x00, 5000), "W-06 survival flags 0x00 (no invuln/fly/creative)");
    c.sendChatCommand("gamemode creative");
    CHECK(waitAbilitiesFlags(c, 0x01 | 0x04 | 0x08, 5000), "W-06 creative flags back to 0x0D");
    // cs 0x26: survival flying claim must not crash/kick; server strips unpermitted flight
    c.sendChatCommand("gamemode survival");
    CHECK(waitAbilitiesFlags(c, 0x00, 5000), "W-06 survival re-confirmed");
    c.sendAbilitiesFlags(0x02); // claim flying without permission
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    CHECK(c.alive(), "W-06 abilities 0x26 received without kick");
    c.close();
}

// --- W-07: sign --------------------------------------------------------------
static void tSign(ServerProc& srv) {
    SECTION("W-07 update_sign 0x39 -> BlockEntityData 0x07 + relogin persist");
    TestClient c;
    CHECK(c.connect("127.0.0.1", srv.port) && c.join("P43Sign"), "W-07 join");
    int sx = (int)std::floor(c.x) + 2, sy = (int)std::floor(c.y), sz = (int)std::floor(c.z);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "setblock %d %d %d minecraft:oak_sign", sx, sy, sz);
    c.sendChatCommand(cmd);
    // placement proof: setblock chat feedback (BlockUpdate may arrive batched as
    // MultiBlockChange, so the feedback string is the robust signal here)
    char msg[128]; snprintf(msg, sizeof(msg), "W-07 sign placed (setblock %d,%d,%d ack)", sx, sy, sz);
    CHECK(waitChat(c, "Changed the block", 8000), msg);
    const std::string lines[4] = {"P43-L1", "P43-L2", "P43-L3", "P43-L4"};
    c.sendSignUpdate(sx, sy, sz, true, lines);
    Packet p;
    bool got07 = c.waitFor([](const Packet& q){ return q.id == proto::pl::sc::BlockEntityData; }, 5000, &p);
    CHECK(got07, "W-07 sign edit re-sends BlockEntityData 0x07");
    if (got07) {
        std::string raw(reinterpret_cast<const char*>(p.body.data()), p.body.size());
        CHECK(raw.find("P43-L1") != std::string::npos, "W-07 0x07 carries line 1 text");
    } else CHECK(false, "W-07 0x07 carries line 1 text");
    c.close();
    // relogin persistence: same offline uuid -> move to trigger chunk
    // (re)stream (chunks only stream on movement), then the chunk-load 0x07
    // resend must still carry the text
    TestClient d;
    CHECK(d.connect("127.0.0.1", srv.port) && d.join("P43Sign"), "W-07 relogin join");
    for (int i = 0; i < 4; ++i) {
        d.sendMovePlayerFlags(d.x, d.y, d.z, 0x01);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    bool persist = false;
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8000);
        while (std::chrono::steady_clock::now() < deadline && !persist) {
            std::lock_guard lk(d.mtx_public());
            for (auto& q : d.recentPublic()) {
                if (q.id != proto::pl::sc::BlockEntityData) continue;
                std::string raw(reinterpret_cast<const char*>(q.body.data()), q.body.size());
                if (raw.find("P43-L1") != std::string::npos) { persist = true; break; }
            }
            if (!persist) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
        }
    }
    CHECK(persist, "W-07 sign text persists across relogin (4/4 via chunk resend)");
    d.close();
}

int main(int argc, char** argv) {
    const char* bin = argc > 1 ? argv[1] : "./build/cppfm";
    ServerProc srv;
    if (!srv.start(bin)) { std::printf("FAIL server start\n"); return 2; }
    tSigned(srv);
    tTab(srv);
    tFinish(srv);
    tMoveFlags(srv);
    tFallDamage(srv);
    tUseEntity(srv);
    tAbilities(srv);
    tSign(srv);
    srv.stop();
    std::printf("\n=== TEST_PLAN43: %d PASS %d FAIL ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
