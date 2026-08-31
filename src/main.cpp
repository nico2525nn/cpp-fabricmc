#include "game/GameServer.hpp"
#include "game/ServerProperties.hpp"
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

using namespace cppfm;

namespace cppfm { extern std::atomic<bool> g_stopRequested; }
using namespace cppfm;
static GameServer* g_server = nullptr;
// POSIX signal handler for SIGINT/SIGTERM — POSIX only, Windows uses SetConsoleCtrlHandler (not implemented, non-portable).
// On Windows SIGTERM is not generated; SIGINT (Ctrl+C) still works via CRT mapping but signal() may fail silently.
static void onSignal(int) {
    g_stopRequested = true;
    if (g_server) g_server->requestStop();   // async-signal-safe subset
}

static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

// Minimal server.properties reader (subset, vanilla-compatible keys) – now via ServerProperties (plan7)
static void loadProperties(ServerConfig& c, const std::string& path) {
    ServerProperties props;
    if (!props.load(path)) return;
    // keep legacy manual handling for compatibility, but prefer typed get<>
    try {
        if (props.has("server-port")) c.port = static_cast<std::uint16_t>(props.get<int>("server-port", c.port));
        if (props.has("max-players")) c.maxPlayers = props.get<int>("max-players", c.maxPlayers);
        c.viewDistance = props.get<int>("view-distance", c.viewDistance);
        c.viewDistance = std::clamp(c.viewDistance, 2, 32);
        c.simulationDistance = props.get<int>("simulation-distance", c.simulationDistance);
        c.simulationDistance = std::clamp(c.simulationDistance, 2, 32);
        if (props.has("motd")) c.motd = props.get<std::string>("motd", c.motd);
        if (props.has("spawn-protection")) c.spawnProtection = std::max(0, props.get<int>("spawn-protection", c.spawnProtection));
        if (props.has("start-time")) c.startTime = props.get<std::int64_t>("start-time", c.startTime);
        if (props.has("level-type")) {
            std::string t = props.get<std::string>("level-type", c.levelType);
            if (t.rfind("minecraft:", 0) == 0) t = t.substr(10);
            c.levelType = (t == "normal") ? "normal" : "flat";
        }
        if (props.has("world-dir")) c.worldDir = props.get<std::string>("world-dir", c.worldDir);
        if (props.has("rcon.port")) c.rcon.port = static_cast<std::uint16_t>(props.get<int>("rcon.port", c.rcon.port));
        if (props.has("rcon.password")) c.rcon.password = props.get<std::string>("rcon.password", c.rcon.password);
        if (props.has("enable-rcon")) c.rcon.enabled = props.get<bool>("enable-rcon", c.rcon.enabled);
        if (props.has("whitelist")) c.whitelist = props.get<bool>("whitelist", c.whitelist);
        if (props.has("online-mode")) c.onlineMode = props.get<bool>("online-mode", c.onlineMode);
        if (props.has("enforce-secure-profile") || props.has("enforcesSecureChat") || props.has("enforces-secure-chat")) {
            bool v = props.get<bool>("enforce-secure-profile", c.enforcesSecureChat);
            v = props.get<bool>("enforcesSecureChat", v);
            v = props.get<bool>("enforces-secure-chat", v);
            c.enforcesSecureChat = v;
        }
        if (props.has("network-compression-threshold") || props.has("compression-threshold")) {
            c.compressionThreshold = props.get<int>("network-compression-threshold", props.get<int>("compression-threshold", c.compressionThreshold));
        }
        // W19 maxLoadedChunks: respect explicit max-loaded-chunks else auto max(8192, viewDist²*4) (plan21 §3)
        // Note: current implementation in GameServer_tick.cpp:334-399 is NOT a simple clear(); it does
        // Chebyshev distance sort + burst limit 16/tick with forced/spawn ticket protection. See GameServer_tick.cpp.
        if (props.has("max-loaded-chunks") || props.has("maxLoadedChunks")) {
            c.maxLoadedChunks = std::max(0, props.get<int>("max-loaded-chunks", props.get<int>("maxLoadedChunks", c.maxLoadedChunks)));
        } else {
            int autoCap = std::max(8192, c.viewDistance * c.viewDistance * 4);
            c.maxLoadedChunks = autoCap;
        }
        if (props.has("io-worker-threads")) c.ioWorkerThreads = std::max(1, props.get<int>("io-worker-threads", c.ioWorkerThreads));
        if (props.has("pvp")) c.pvp = props.get<bool>("pvp", c.pvp);
        if (props.has("allow-flight")) c.allowFlight = props.get<bool>("allow-flight", c.allowFlight);
        if (props.has("hardcore")) c.hardcore = props.get<bool>("hardcore", c.hardcore);
        // max-players already handled above; keep fallback for hyphen variant
        // online-mode / enforce-secure-profile already handled above
    } catch (...) {}
}

int main(int argc, char** argv) {
    ServerConfig cfg;
    loadProperties(cfg, "server.properties");
    auto apply = [&](const std::string& k, const std::string& v) {
        try {
            if (k == "port") cfg.port = static_cast<std::uint16_t>(std::stoi(v));
            else if (k == "view-distance") cfg.viewDistance = std::clamp(std::stoi(v), 2, 32);
            else if (k == "assets") cfg.assetsDir = v;
            else if (k == "motd") cfg.motd = v;
            else if (k == "world-dir") cfg.worldDir = v;
            else if (k == "spawn-protection") cfg.spawnProtection = std::max(0, std::stoi(v));
            else if (k == "level-type") {
                std::string t = v;
                if (t.rfind("minecraft:", 0) == 0) t = t.substr(10);
                cfg.levelType = (t == "normal") ? "normal" : "flat";
            }
            else if (k == "start-time") cfg.startTime = std::stoll(v);
            else if (k == "rcon.port") cfg.rcon.port = (uint16_t)std::stoi(v);
            else if (k == "rcon.password") cfg.rcon.password = v;
            else if (k == "enable-rcon") cfg.rcon.enabled = (v == "true");
            else if (k == "whitelist") cfg.whitelist = (v == "true");
            else if (k == "online-mode") cfg.onlineMode = (v == "true");
            else if (k == "enforcesSecureChat" || k == "enforce-secure-profile" || k == "enforces-secure-chat") cfg.enforcesSecureChat = (v == "true");
            else if (k == "pvp") cfg.pvp = (v == "true");
            else if (k == "allow-flight") cfg.allowFlight = (v == "true");
            else if (k == "hardcore") cfg.hardcore = (v == "true");
            else if (k == "max-players") cfg.maxPlayers = std::max(0, std::stoi(v));
        } catch (...) {}
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--", 0) != 0) continue;
        auto eq = a.find('=');
        if (eq != std::string::npos) { apply(a.substr(2, eq - 2), a.substr(eq + 1)); continue; }
        const std::string k = a.substr(2);
        if (i + 1 < argc) apply(k, argv[++i]);
    }

    GameServer server(cfg);
    g_server = &server;
    // POSIX signal handling — SIGINT/SIGTERM via std::signal. POSIX-dependent; on Windows
    // SIGTERM is not reliably generated and signal() semantics differ. Use SetConsoleCtrlHandler
    // for full Windows support (future work). Current handler sets atomic flag + requestStop().
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try {
        server.init();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] asset load failed: %s\n", e.what());
        return 1;
    }

    std::printf("[cppfm] CppFabricMC starting: port=%u view=%d biome=%s world=%s level=%s\n",
                cfg.port, cfg.viewDistance, cfg.worldBiome.c_str(), cfg.worldDir.c_str(),
                cfg.levelType.c_str());
    try {
        server.runForever();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] fatal: %s\n", e.what());
        return 1;
    }
    std::printf("[cppfm] bye\n");
    return 0;
}
