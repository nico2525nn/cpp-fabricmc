// check_world: offline world-integrity checker (plan46 §2, O-08).
// Usage: check_world <worldDir>
// Checks: level.dat (+level.dat_old fallback) parse + DataVersion,
//         region/*.mca per-chunk header/zlib sanity, playerdata/*.dat parse,
//         session.lock state.
// Exit: 0 = OK, 1 = repairable (bad chunks quarantinable / version drift /
//         stale lock), 2 = fatal (level.dat and _old both unreadable).
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../src/core/NBTValue.hpp"
#include "../src/core/Zlib.hpp"
#include "../src/game/SessionLock.hpp"

namespace fs = std::filesystem;
using cppfm::nbt::Parser;
using cppfm::ReadBuffer;

namespace {

struct Report {
    int badChunks = 0;
    int okChunks = 0;
    int badPlayers = 0;
    int okPlayers = 0;
    bool levelOk = false;
    bool levelOldOk = false;
    bool versionDrift = false;
};

bool parseRootFile(const std::string& path, cppfm::nbt::Value& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    if (bytes.empty()) return false;
    try {
        ReadBuffer in(bytes);
        Parser p(in);
        out = p.readFileRoot();
        return out.get("Data") != nullptr || out.get("Level") != nullptr;
    } catch (...) { return false; }
}

// Raw region scan: header offsets -> per-chunk [len][comp] + zlib trial.
// Returns {okChunks, badChunks} and prints bad chunk coords.
std::pair<int,int> scanRegion(const std::string& mcaPath) {
    int ok = 0, bad = 0;
    std::ifstream f(mcaPath, std::ios::binary);
    if (!f) return {0, 0};
    std::uint8_t header[8192];
    if (!f.read(reinterpret_cast<char*>(header), sizeof(header))) {
        std::printf("  [BAD] %s: header unreadable\n", mcaPath.c_str());
        return {0, 1};
    }
    // region coords from filename r.X.Z.mca
    std::string rx = "?", rz = "?";
    try {
        auto base = fs::path(mcaPath).filename().string(); // r.X.Z.mca
        auto p1 = base.find('.'), p2 = base.find('.', p1 + 1), p3 = base.find('.', p2 + 1);
        rx = base.substr(p1 + 1, p2 - p1 - 1);
        rz = base.substr(p2 + 1, p3 - p2 - 1);
    } catch (...) {}
    f.seekg(0, std::ios::end);
    const long fileSize = static_cast<long>(f.tellg());
    for (int i = 0; i < 1024; ++i) {
        const std::uint32_t off =
            (std::uint32_t(header[i*4]) << 16) | (std::uint32_t(header[i*4+1]) << 8) |
            std::uint32_t(header[i*4+2]);
        const unsigned count = header[i*4+3];
        if (off == 0 || count == 0) continue;
        const int lx = i % 32, lz = i / 32;
        bool good = false;
        do {
            if (static_cast<long>(off) * 4096 + 5 > fileSize) break;
            f.seekg(static_cast<long>(off) * 4096);
            std::uint8_t lb[4];
            if (!f.read(reinterpret_cast<char*>(lb), 4)) break;
            const std::uint32_t total =
                (std::uint32_t(lb[0]) << 24) | (std::uint32_t(lb[1]) << 16) |
                (std::uint32_t(lb[2]) << 8) | std::uint32_t(lb[3]);
            if (total < 2 || total > 8u * 1024 * 1024) break;
            if (static_cast<long>(off) * 4096 + 4 + static_cast<long>(total) > fileSize) break;
            std::uint8_t comp = 0;
            if (!f.read(reinterpret_cast<char*>(&comp), 1)) break;
            std::vector<std::uint8_t> raw(total - 1);
            if (!f.read(reinterpret_cast<char*>(raw.data()), raw.size())) break;
            try {
                if (comp == 2) {
                    std::vector<std::uint8_t> out;
                    cppfm::decompressUnknown(raw.data(), raw.size(), out);
                    if (out.empty()) break;
                    // must be a chunk NBT root
                    ReadBuffer in(out);
                    Parser p(in);
                    cppfm::nbt::Value root = p.readFileRoot();
                    (void)root;
                } else if (comp == 1) {
                    break; // gzip regions unsupported by server
                } // comp==0 uncompressed: accept header-level only
                good = true;
            } catch (...) { good = false; }
        } while (false);
        if (good) ++ok;
        else {
            ++bad;
            std::printf("  [BAD] r.%s.%s chunk local (%d,%d): header/zlib/NBT mismatch\n",
                        rx.c_str(), rz.c_str(), lx, lz);
        }
    }
    return {ok, bad};
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: check_world <worldDir>\n");
        return 2;
    }
    const std::string dir = argv[1];
    Report r;
    int exitCode = 0;

    std::printf("[check_world] dir=%s\n", dir.c_str());

    // 1. level.dat
    cppfm::nbt::Value root = cppfm::nbt::Value::makeCompound();
    r.levelOk = parseRootFile(dir + "/level.dat", root);
    std::printf("  level.dat: %s\n", r.levelOk ? "OK" : "UNREADABLE");
    if (r.levelOk) {
        if (const auto* d = root.get("Data")) {
            if (const auto* dv = d->get("DataVersion")) {
                long ver = -1;
                if (dv->tag == cppfm::nbt::Int) ver = dv->i;
                else if (dv->tag == cppfm::nbt::Long) ver = static_cast<long>(dv->l);
                std::printf("  DataVersion: %ld (current 4189)\n", ver);
                if (ver != 4189) {
                    r.versionDrift = true;
                    std::printf("  [WARN] DataVersion drift (auto-upgraded on load)\n");
                }
            } else {
                r.versionDrift = true;
                std::printf("  [WARN] DataVersion missing\n");
            }
        }
    } else {
        cppfm::nbt::Value old = cppfm::nbt::Value::makeCompound();
        r.levelOldOk = parseRootFile(dir + "/level.dat_old", old);
        std::printf("  level.dat_old: %s\n", r.levelOldOk ? "OK (recovery available)" : "UNREADABLE");
        if (!r.levelOldOk) {
            std::printf("[check_world] FATAL: no usable level file\n");
            return 2;
        }
    }

    // 2. regions
    const std::string regionDir = dir + "/region";
    if (fs::exists(regionDir)) {
        for (auto& e : fs::directory_iterator(regionDir)) {
            if (e.path().extension() != ".mca") continue;
            auto [ok, bad] = scanRegion(e.path().string());
            r.okChunks += ok;
            r.badChunks += bad;
        }
    }
    std::printf("  chunks: ok=%d bad=%d\n", r.okChunks, r.badChunks);
    if (r.badChunks > 0) {
        std::printf("  [WARN] %d bad chunk(s): restart will regenerate them (O-07)\n", r.badChunks);
        exitCode = 1;
    }

    // 3. playerdata
    const std::string pdDir = dir + "/playerdata";
    if (fs::exists(pdDir)) {
        for (auto& e : fs::directory_iterator(pdDir)) {
            if (e.path().extension() != ".dat") continue;
            cppfm::nbt::Value p = cppfm::nbt::Value::makeCompound();
            std::ifstream f(e.path(), std::ios::binary);
            bool ok = false;
            if (f) {
                std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                                 std::istreambuf_iterator<char>());
                if (!bytes.empty()) {
                    try {
                        ReadBuffer in(bytes);
                        Parser pp(in);
                        p = pp.readFileRoot();
                        ok = true;
                    } catch (...) { ok = false; }
                }
            }
            if (ok) ++r.okPlayers;
            else {
                ++r.badPlayers;
                std::printf("  [BAD] playerdata %s unreadable (will be quarantined on load)\n",
                            e.path().filename().string().c_str());
            }
        }
    }
    std::printf("  playerdata: ok=%d bad=%d\n", r.okPlayers, r.badPlayers);
    if (r.badPlayers > 0) exitCode = 1;

    // 4. session.lock
    {
        const std::string lp = dir + "/session.lock";
        if (!fs::exists(lp)) {
            std::printf("  session.lock: absent (server stopped or never started)\n");
        } else {
            std::ifstream f(lp, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
            long pid = cppfm::SessionLock::parsePid(content);
            bool alive = cppfm::SessionLock::pidAlive(pid);
            std::printf("  session.lock: pid=%ld %s (informational only)\n", pid, alive ? "LIVE (server may be running)" : "stale");
        }
    }

    if (r.versionDrift && exitCode == 0) exitCode = 1;
    std::printf("[check_world] result=%s (exit %d)\n",
                exitCode == 0 ? "OK" : (exitCode == 1 ? "REPAIRABLE" : "FATAL"), exitCode);
    return exitCode;
}
