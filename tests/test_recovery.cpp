// test_recovery — plan46 §2 (O-07/O-08): crash recovery + session.lock.
// Self-contained unit test (no server spawn):
//  - loadWithRecovery 3-stage matrix (dat / dat_old / fresh)
//  - corruption injection matrix: truncate × bitflip × empty
//  - playerdata isolation + quarantine
//  - session.lock acquire/release/stale
//  - corrupt region entry -> RegionFile::load empty (regenerate path)
#include "../src/game/World.hpp"
#include "../src/game/WorldDataManager.hpp"
#include "../src/game/SessionLock.hpp"
#include "../src/game/PlayerDataRecovery.hpp"
#include "../src/game/RegionFile.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace cppfm;

static int g_fail = 0;
static int g_pass = 0;
#define CHECK(cond, msg) do { \
    const bool c_ = static_cast<bool>(cond); \
    std::printf("  %s  %s\n", c_ ? " ok " : "FAIL", msg); \
    if (c_) ++g_pass; else ++g_fail; } while (0)

static std::string mkTmp(const char* tag) {
    char tmpl[128];
    std::snprintf(tmpl, sizeof(tmpl), "/tmp/opencode/recov_%s_XXXXXX", tag);
    if (!::mkdtemp(tmpl)) { std::fprintf(stderr, "mkdtemp failed\n"); std::exit(2); }
    return std::string(tmpl);
}

static void writeFile(const std::string& p, const std::vector<std::uint8_t>& d) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(d.data()), d.size());
}
static std::vector<std::uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
}

static World makeWorld() { return World("minecraft:plains", LevelType::Normal, 12345ULL); }

// Save a valid level.dat (+_old via second save) into dir. Returns raw bytes.
static std::vector<std::uint8_t> saveGoodLevel(const std::string& dir) {
    World w = makeWorld();
    WorldDataManager m(dir);
    std::string diff = "normal";
    double dia = 59999968.0, cx = 0, cz = 0;
    bool ok = m.saveLevelDataWithProviders(100, 1000, w, diff, dia, cx, cz);
    CHECK(ok, "setup: saveLevelDataWithProviders ok");
    // second save produces level.dat_old backup
    ok = m.saveLevelDataWithProviders(200, 2000, w, diff, dia, cx, cz);
    CHECK(ok, "setup: second save (dat_old backup) ok");
    CHECK(fs::exists(dir + "/level.dat"), "setup: level.dat exists");
    CHECK(fs::exists(dir + "/level.dat_old"), "setup: level.dat_old exists");
    return readFile(dir + "/level.dat");
}

static void corruptTruncate(const std::string& p) {
    auto b = readFile(p);
    if (b.size() > 12) b.resize(12);
    writeFile(p, b);
}
static void corruptBitflip(const std::string& p) {
    auto b = readFile(p);
    for (std::size_t i = 0; i < b.size(); i += 7) b[i] ^= 0xFF;
    if (b.empty()) b.push_back(0xFF);
    writeFile(p, b);
}
static void corruptEmpty(const std::string& p) {
    writeFile(p, {});
}

int main() {
    std::printf("=== test_recovery — plan46 §2 O-07/O-08 ===\n");

    // -- 1) happy path: level.dat loads, source=Dat ---------------------------
    {
        std::string dir = mkTmp("happy");
        saveGoodLevel(dir);
        World w = makeWorld();
        WorldDataManager m(dir);
        std::string diff = "peaceful";
        double dia = 1, cx = 9, cz = 9;
        RecoveryResult r;
        bool ok = m.loadWithRecovery(w, diff, dia, cx, cz, nullptr, nullptr, r);
        CHECK(ok && r.ok && r.src == LevelSource::Dat, "happy: src=Dat ok=1");
        CHECK(!r.logLines.empty(), "happy: logLines non-empty (O-07c)");
        CHECK(diff == "normal", "happy: difficulty round-trips");
    }

    // -- 2) corruption matrix: dat bad × {truncate,bitflip,empty}, old good ---
    const char* kinds[3] = {"truncate", "bitflip", "empty"};
    for (int k = 0; k < 3; ++k) {
        std::string dir = mkTmp(kinds[k]);
        saveGoodLevel(dir);
        if (k == 0) corruptTruncate(dir + "/level.dat");
        else if (k == 1) corruptBitflip(dir + "/level.dat");
        else corruptEmpty(dir + "/level.dat");
        World w = makeWorld();
        WorldDataManager m(dir);
        std::string diff = "x";
        double dia = 1, cx = 9, cz = 9;
        RecoveryResult r;
        bool ok = m.loadWithRecovery(w, diff, dia, cx, cz, nullptr, nullptr, r);
        char msg[160];
        std::snprintf(msg, sizeof(msg), "matrix %s: falls back to DatOld ok=1", kinds[k]);
        CHECK(ok && r.ok && r.src == LevelSource::DatOld, msg);
        std::snprintf(msg, sizeof(msg), "matrix %s: corrupt dat quarantined", kinds[k]);
        CHECK(fs::exists(dir + "/level.dat.corrupt"), msg);
        std::snprintf(msg, sizeof(msg), "matrix %s: logLines>=2 (attempt+source)", kinds[k]);
        CHECK(r.logLines.size() >= 2, msg);
    }

    // -- 3) both bad -> Fresh + caller-visible failure -------------------------
    {
        std::string dir = mkTmp("fresh");
        saveGoodLevel(dir);
        corruptBitflip(dir + "/level.dat");
        corruptTruncate(dir + "/level.dat_old");
        World w = makeWorld();
        WorldDataManager m(dir);
        std::string diff = "x";
        double dia = 1, cx = 9, cz = 9;
        RecoveryResult r;
        bool ok = m.loadWithRecovery(w, diff, dia, cx, cz, nullptr, nullptr, r);
        CHECK(!ok && !r.ok && r.src == LevelSource::Fresh, "both-bad: src=Fresh ok=0");
        CHECK(!r.logLines.empty(), "both-bad: logLines non-empty (O-07c)");
    }

    // -- 4) playerdata isolation ------------------------------------------------
    {
        std::string dir = mkTmp("player");
        fs::create_directories(dir);
        // 4a: missing file -> false, no quarantine
        {
            bool ok = loadPlayerDataIsolated(dir + "/nobody.dat",
                                             [](const std::string&) { return true; });
            CHECK(!ok && !fs::exists(dir + "/nobody.dat.corrupt"), "playerdata: missing -> false, no quarantine");
        }
        // 4b: corrupt file -> false + quarantine
        {
            const std::string p = dir + "/bad.dat";
            writeFile(p, std::vector<std::uint8_t>{0x01, 0x02, 0x03, 0xFF});
            std::string q;
            bool ok = loadPlayerDataIsolated(p,
                                             [](const std::string&) { return false; }, &q);
            CHECK(!ok && fs::exists(p + ".corrupt") && !fs::exists(p), "playerdata: corrupt -> quarantined");
            CHECK(!q.empty(), "playerdata: quarantine path reported");
        }
        // 4c: good file -> true, untouched
        {
            const std::string p = dir + "/good.dat";
            writeFile(p, std::vector<std::uint8_t>{0x0A, 0x00});
            bool ok = loadPlayerDataIsolated(p,
                                             [](const std::string&) { return true; });
            CHECK(ok && fs::exists(p) && !fs::exists(p + ".corrupt"), "playerdata: good -> true, untouched");
        }
        // 4d: loader throws -> false + quarantine
        {
            const std::string p = dir + "/throw.dat";
            writeFile(p, std::vector<std::uint8_t>{0x00});
            bool ok = loadPlayerDataIsolated(p, [](const std::string&) -> bool {
                throw std::runtime_error("boom");
            });
            CHECK(!ok && fs::exists(p + ".corrupt"), "playerdata: throwing loader -> quarantined");
        }
    }

    // -- 5) session.lock ---------------------------------------------------------
    {
        std::string dir = mkTmp("lock");
        SessionLock a;
        bool live = true;
        CHECK(a.acquire(dir, live) && a.held() && !live, "sessionlock: acquire ok, no live holder");
        {
            std::ifstream f(dir + "/session.lock");
            std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            CHECK(SessionLock::parsePid(c) == SessionLock::selfPid(), "sessionlock: lock holds our pid");
        }
        a.release();
        CHECK(!fs::exists(dir + "/session.lock"), "sessionlock: release removes file");
        // stale lock (dead pid) -> acquire proceeds, no live flag
        {
            std::ofstream f(dir + "/session.lock", std::ios::trunc);
            f << "99999999 1\n";
        }
        SessionLock b;
        bool live2 = true;
        CHECK(b.acquire(dir, live2) && !live2, "sessionlock: stale lock overwritten, live=0");
    }

    // -- 6) corrupt region entry -> load empty (regenerate, O-07b) ---------------
    {
        std::string dir = mkTmp("region");
        const std::string mca = dir + "/r.0.0.mca";
        // header: entry (0,0) -> sector 2, 1 sector; rest zero
        std::vector<std::uint8_t> hdr(8192, 0);
        hdr[0] = 0; hdr[1] = 0; hdr[2] = 2; hdr[3] = 1;
        std::vector<std::uint8_t> sector(4096, 0);
        // length=6, comp=2, 5 bytes of garbage zlib
        sector[0] = 0; sector[1] = 0; sector[2] = 0; sector[3] = 6;
        sector[4] = 2;
        sector[5] = 0x78; sector[6] = 0x9C; sector[7] = 0xFF; sector[8] = 0xFF; sector[9] = 0xFF;
        writeFile(mca, hdr);
        std::ofstream f(mca, std::ios::binary | std::ios::app);
        f.write(reinterpret_cast<const char*>(sector.data()), sector.size());
        f.close();
        RegionFile rf(mca);
        // NB: RegionFile::load may throw on garbage zlib; Persistence::loadChunk
        // catches it and regenerates. Mirror that contract here.
        bool regen = false;
        try {
            auto bytes = rf.load(0, 0);
            regen = bytes.empty();
        } catch (...) { regen = true; }
        CHECK(regen, "region: garbage chunk -> regenerate path (O-07b)");
        auto missing = rf.load(5, 5);
        CHECK(missing.empty(), "region: absent chunk -> empty");
    }

    std::printf("test_recovery: %d PASS %d FAIL\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
