// PlayerDataRecovery: isolated playerdata load with quarantine (plan46 §2, O-07(b)). A corrupt player .dat must never prevent startup nor
// wipe neighbours: the bad file is renamed to *.dat.corrupt (forensics) and the player starts fresh. Header-only + loader-injected so unit
// tests can exercise it without GameServer.
#pragma once
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>

namespace cppfm {

// Wraps loader(path) -> bool. Returns loader's result on success. When the file exists but loader reports failure
// (corrupt/truncated/foreign), quarantines it to path+".corrupt" and returns false (caller spawns a fresh player). When the file is simply
// absent (first join), returns false with no quarantine.
template <typename Loader>
bool loadPlayerDataIsolated(const std::string& path, Loader&& loader,
                            std::string* quarantinedOut = nullptr) {
    bool exists = false;
    try {
        exists = std::filesystem::exists(path);
    } catch (...) { exists = false; }
    if (!exists) return false;
    bool ok = false;
    try {
        ok = loader(path);
    } catch (...) { ok = false; }
    if (ok) return true;
    try {
        const std::string q = path + ".corrupt";
        std::error_code ec;
        std::filesystem::rename(path, q, ec);
        if (!ec) {
            std::fprintf(stderr,
                         "[cppfm] playerdata %s unreadable, quarantined to %s (fresh spawn)\n",
                         path.c_str(), q.c_str());
            if (quarantinedOut) *quarantinedOut = q;
        } else {
            std::fprintf(stderr,
                         "[cppfm] playerdata %s unreadable, quarantine failed (%s)\n",
                         path.c_str(), ec.message().c_str());
        }
    } catch (...) {}
    return false;
}

} // namespace cppfm
