#include "game/GameServer.hpp"
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

using namespace cppfm;

static GameServer* g_server = nullptr;
static void onSignal(int) {
    if (g_server) g_server->stop();
}

static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

// Minimal server.properties reader (subset, vanilla-compatible keys)
static void loadProperties(ServerConfig& c, const std::string& path) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq);
        const std::string v = line.substr(eq + 1);
        try {
            if (k == "server-port") c.port = static_cast<std::uint16_t>(std::stoi(v));
            else if (k == "max-players") c.maxPlayers = std::stoi(v);
            else if (k == "view-distance") c.viewDistance = std::clamp(std::stoi(v), 2, 32);
            else if (k == "simulation-distance") c.simulationDistance = std::clamp(std::stoi(v), 2, 32);
            else if (k == "motd") c.motd = v;
        else if (k == "start-time") c.startTime = std::stoll(v);
        else if (k == "level-type") {
            std::string t = v;
            if (t.rfind("minecraft:", 0) == 0) t = t.substr(10);
            c.levelType = (t == "normal") ? "normal" : "flat";
        }
        else if (k == "world-dir") c.worldDir = v;
        else if (k == "rcon.port") c.rcon.port = static_cast<std::uint16_t>(std::stoi(v));
        else if (k == "rcon.password") c.rcon.password = v;
        else if (k == "enable-rcon") c.rcon.enabled = (v == "true");
        else if (k == "whitelist") c.whitelist = (v == "true");
        else if (k == "online-mode") c.onlineMode = (v == "true");
        else if (k == "online-mode") c.onlineMode = (v == "true");
        else if (k == "compression-threshold") c.compressionThreshold = std::stoi(v);
        else if (k == "world-dir") c.worldDir = v;
        } catch (...) {}
    }
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
