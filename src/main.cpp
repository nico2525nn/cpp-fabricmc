#include "game/GameServer.hpp"
#include "game/ServerProperties.hpp"
#include "core/RuntimeLayout.hpp"
#include <charconv>
#include <csignal>
#include <cstdio>
#include <string>
#include <string_view>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace cppfm;

namespace cppfm { extern std::atomic<bool> g_stopRequested; }
using namespace cppfm;
static GameServer* g_server = nullptr;
// POSIX signal handler for SIGINT/SIGTERM. Windows installs the equivalent
// SetConsoleCtrlHandler callback below.
// On Windows SIGTERM is not generated; SIGINT (Ctrl+C) still works via CRT mapping but signal() may fail silently.
static void onSignal(int) {
    g_stopRequested = true;
    if (g_server) g_server->requestStop();   // async-signal-safe subset
}

#ifdef _WIN32
static BOOL WINAPI onConsoleControl(DWORD control) {
    switch (control) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        onSignal(0);
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

template<typename T>
static bool parseInteger(std::string_view text, T& value) {
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

static bool parseBool(std::string_view text, bool& value) {
    if (text == "true" || text == "1" || text == "yes" || text == "on") {
        value = true;
        return true;
    }
    if (text == "false" || text == "0" || text == "no" || text == "off") {
        value = false;
        return true;
    }
    return false;
}

static bool parsePort(std::string_view text, std::uint16_t& port) {
    int parsed = 0;
    // Port zero asks the OS for an ephemeral listener.  It is useful for
    // isolated integration probes and is accepted by BSD sockets and
    // Winsock; normal server.properties still defaults to 25565.
    if (!parseInteger(text, parsed) || parsed < 0 || parsed > 65535) return false;
    port = static_cast<std::uint16_t>(parsed);
    return true;
}

static void printUsage(const char* program) {
    std::printf(
        "Usage: %s [options]\n"
        "\n"
        "CppFabricMC Minecraft Java Edition 1.21.4 server (protocol 769).\n"
        "\n"
        "Options use --name=value or --name value.\n"
        "  --help, -h                 Show this help and exit\n"
        "  --version                  Show the server version and exit\n"
        "  --port=<0..65535>          Listen port (0 selects an ephemeral port)\n"
        "  --world-dir=<path>         World/server directory\n"
        "  --level-type=<normal|flat> World generator type\n"
        "  --view-distance=<2..32>    Client view distance\n"
        "  --jvm=<true|false>         Enable the embedded Java boundary (default on)\n"
        "  --jvm-strict=<true|false>  Fail startup when Java integration is unavailable\n"
        "  --jvm-mods=<path>          Server-side Java mod directory\n"
        "  --enable-rcon=<true|false> Enable Source RCON\n",
        program ? program : "cppfm");
}

static void printVersion() {
    std::printf("CppFabricMC %s (protocol %d)\n",
                proto::kMinecraftVersion, proto::kProtocolVersion);
}

static void loadProperties(ServerConfig& c, const std::string& path) {
    ServerProperties props;
    if (!props.load(path)) return;
    if (props.has("server-port")) parsePort(props.get<std::string>("server-port"), c.port);
    if (props.has("max-players")) c.maxPlayers = std::max(0, props.get<int>("max-players", c.maxPlayers));
    c.viewDistance = std::clamp(props.get<int>("view-distance", c.viewDistance),
                                constants::kViewDistanceMin, constants::kViewDistanceMax);
    c.simulationDistance = std::clamp(props.get<int>("simulation-distance", c.simulationDistance), 2, 32);
    if (props.has("motd")) c.motd = props.get<std::string>("motd", c.motd);
    if (props.has("spawn-protection")) c.spawnProtection = std::max(0, props.get<int>("spawn-protection", c.spawnProtection));
    if (props.has("start-time")) c.startTime = props.get<std::int64_t>("start-time", c.startTime);
    if (props.has("level-type")) {
        std::string t = props.get<std::string>("level-type", c.levelType);
        if (t.rfind("minecraft:", 0) == 0) t = t.substr(10);
        c.levelType = (t == "normal") ? "normal" : "flat";
    }
    if (props.has("world-dir")) c.worldDir = props.get<std::string>("world-dir", c.worldDir);
    if (props.has("rcon.port")) parsePort(props.get<std::string>("rcon.port"), c.rcon.port);
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
        c.compressionThreshold = std::max(-1, props.get<int>(
            "network-compression-threshold", props.get<int>("compression-threshold", c.compressionThreshold)));
    }
    if (props.has("max-loaded-chunks") || props.has("maxLoadedChunks")) {
        c.maxLoadedChunks = std::max(0, props.get<int>(
            "max-loaded-chunks", props.get<int>("maxLoadedChunks", c.maxLoadedChunks)));
    } else {
        c.maxLoadedChunks = std::max(8192, c.viewDistance * c.viewDistance * 4);
    }
    if (props.has("io-worker-threads"))
        c.ioWorkerThreads = std::clamp(props.get<int>("io-worker-threads", c.ioWorkerThreads), 1, 64);
    if (props.has("pvp")) c.pvp = props.get<bool>("pvp", c.pvp);
    if (props.has("allow-flight")) c.allowFlight = props.get<bool>("allow-flight", c.allowFlight);
    if (props.has("hardcore")) c.hardcore = props.get<bool>("hardcore", c.hardcore);
    if (props.has("jvm") || props.has("jvm-enabled"))
        c.jvmEnabled = props.get<bool>("jvm", props.get<bool>("jvm-enabled", c.jvmEnabled));
    if (props.has("jvm-strict")) c.jvmStrict = props.get<bool>("jvm-strict", c.jvmStrict);
    if (props.has("jvm-classes")) c.jvmClassesDir = props.get<std::string>("jvm-classes", c.jvmClassesDir);
    if (props.has("jvm-mods")) c.jvmModsDir = props.get<std::string>("jvm-mods", c.jvmModsDir);
    if (props.has("jvm-config")) c.jvmConfigDir = props.get<std::string>("jvm-config", c.jvmConfigDir);
    if (props.has("jvm-java-home")) c.jvmJavaHome = props.get<std::string>("jvm-java-home", c.jvmJavaHome);
    if (props.has("jvm-library")) c.jvmLibrary = props.get<std::string>("jvm-library", c.jvmLibrary);
    if (props.has("jvm-libraries")) c.jvmLibrariesDir = props.get<std::string>("jvm-libraries", c.jvmLibrariesDir);
}

int main(int argc, char** argv) {
    // Handle informational flags before RuntimeLayout::prepare(): asking for
    // help or a version must not create a server directory, acquire a lock, or
    // start the embedded JVM as a side effect.
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i] ? argv[i] : "");
        if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        if (argument == "--version") {
            printVersion();
            return 0;
        }
    }
    std::filesystem::path serverRoot;
    std::string layoutError;
    if (!RuntimeLayout::prepare(serverRoot, &layoutError)) {
        std::fprintf(stderr, "[cppfm] runtime layout setup failed: %s\n",
                     layoutError.c_str());
        return 1;
    }
    ServerConfig cfg;
    loadProperties(cfg, "server.properties");
    if (cfg.jvmClassesDir.empty())
        cfg.jvmClassesDir = RuntimeLayout::embeddedClasses(serverRoot).string();
    if (cfg.jvmLibrariesDir.empty())
        cfg.jvmLibrariesDir = (serverRoot / "libraries").string();
    auto apply = [&](const std::string& k, const std::string& v) {
        auto invalid = [&] {
            std::fprintf(stderr, "[cppfm] invalid command-line value for --%s: %s\n",
                         k.c_str(), v.c_str());
        };
        int intValue = 0;
        std::int64_t longValue = 0;
        bool boolValue = false;
        if (k == "port") {
            if (!parsePort(v, cfg.port)) invalid();
        } else if (k == "view-distance") {
            if (!parseInteger(v, intValue)) invalid();
            else cfg.viewDistance = std::clamp(intValue, constants::kViewDistanceMin, constants::kViewDistanceMax);
        } else if (k == "assets") cfg.assetsDir = v;
        else if (k == "motd") cfg.motd = v;
        else if (k == "world-dir") cfg.worldDir = v;
        else if (k == "spawn-protection") {
            if (!parseInteger(v, intValue)) invalid();
            else cfg.spawnProtection = std::max(0, intValue);
        } else if (k == "level-type") {
            std::string t = v;
            if (t.rfind("minecraft:", 0) == 0) t = t.substr(10);
            cfg.levelType = (t == "normal") ? "normal" : "flat";
        } else if (k == "start-time") {
            if (!parseInteger(v, longValue)) invalid();
            else cfg.startTime = longValue;
        } else if (k == "rcon.port") {
            if (!parsePort(v, cfg.rcon.port)) invalid();
        } else if (k == "rcon.password") cfg.rcon.password = v;
        else if (k == "enable-rcon") {
            if (!parseBool(v, boolValue)) invalid(); else cfg.rcon.enabled = boolValue;
        } else if (k == "whitelist") {
            if (!parseBool(v, boolValue)) invalid(); else cfg.whitelist = boolValue;
        } else if (k == "online-mode") {
            if (!parseBool(v, boolValue)) invalid(); else cfg.onlineMode = boolValue;
        } else if (k == "enforcesSecureChat" || k == "enforce-secure-profile" || k == "enforces-secure-chat") {
            if (!parseBool(v, boolValue)) invalid(); else cfg.enforcesSecureChat = boolValue;
        } else if (k == "pvp") {
            if (!parseBool(v, boolValue)) invalid(); else cfg.pvp = boolValue;
        } else if (k == "allow-flight") {
            if (!parseBool(v, boolValue)) invalid(); else cfg.allowFlight = boolValue;
        } else if (k == "hardcore") {
            if (!parseBool(v, boolValue)) invalid(); else cfg.hardcore = boolValue;
        } else if (k == "max-players") {
            if (!parseInteger(v, intValue)) invalid();
            else cfg.maxPlayers = std::max(0, intValue);
        } else if (k == "jvm" || k == "jvm-enabled") {
            if (!parseBool(v, boolValue)) invalid(); else cfg.jvmEnabled = boolValue;
        } else if (k == "jvm-strict") {
            if (!parseBool(v, boolValue)) invalid(); else cfg.jvmStrict = boolValue;
        } else if (k == "jvm-classes") cfg.jvmClassesDir = v;
        else if (k == "jvm-mods") cfg.jvmModsDir = v;
        else if (k == "jvm-config") cfg.jvmConfigDir = v;
        else if (k == "jvm-java-home") cfg.jvmJavaHome = v;
        else if (k == "jvm-library") cfg.jvmLibrary = v;
        else if (k == "jvm-libraries") cfg.jvmLibrariesDir = v;
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--", 0) != 0) continue;
        auto eq = a.find('=');
        if (eq != std::string::npos) { apply(a.substr(2, eq - 2), a.substr(eq + 1)); continue; }
        const std::string k = a.substr(2);
        if (i + 1 < argc && std::string_view(argv[i + 1]).rfind("--", 0) != 0)
            apply(k, argv[++i]);
        else
            std::fprintf(stderr, "[cppfm] missing value for --%s\n", k.c_str());
    }

    GameServer server(cfg);
    g_server = &server;
    // Use the CRT signal path on Unix and the native console callback on
    // Windows; both converge on the same idempotent server stop request.
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
#ifdef _WIN32
    SetConsoleCtrlHandler(onConsoleControl, TRUE);
#endif

    try {
        server.init();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[cppfm] asset load failed: %s\n", e.what());
        return 1;
    }

    // A console signal can arrive while the resource/world/JVM bootstrap is
    // still running.  Do not enter runForever() after that early stop request
    // or the request would be lost when the listener is created.
    if (g_stopRequested.load(std::memory_order_acquire)) return 0;

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
