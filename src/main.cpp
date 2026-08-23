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
        else if (k == "compression-threshold") c.compressionThreshold = std::stoi(v);
        } catch (...) {}
    }
}

int main(int argc, char** argv) {
    ServerConfig cfg;
    loadProperties(cfg, "server.properties");
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) cfg.port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        else if (a == "--view-distance" && i + 1 < argc) cfg.viewDistance = std::clamp(std::stoi(argv[++i]), 2, 32);
        else if (a == "--assets" && i + 1 < argc) cfg.assetsDir = argv[++i];
        else if (a == "--motd" && i + 1 < argc) cfg.motd = argv[++i];
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

    std::printf("[cppfm] CppFabricMC starting: port=%u view=%d biome=%s\n",
                cfg.port, cfg.viewDistance, cfg.worldBiome.c_str());
    try {
        server.runForever();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] fatal: %s\n", e.what());
        return 1;
    }
    std::printf("[cppfm] bye\n");
    return 0;
}
