#include "game/GameServer.hpp"
#include "game/ServerConfig.hpp"
#include "core/RuntimeLayout.hpp"
#include <csignal>
#include <cstdio>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>
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
        "  --world-dir=<path>         World save directory (server.properties stays in the server root)\n"
        "  --level-type=<normal|flat> World generator type\n"
        "  --view-distance=<2..32>    Client view distance\n"
        "  --simulation-distance=<2..32>\n"
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

static void printConfigDiagnostics(const ConfigDiagnostics& diagnostics, bool commandLine) {
    for (const auto& diagnostic : diagnostics.entries) {
        switch (diagnostic.kind) {
        case ConfigDiagnosticKind::InvalidValue:
            if (commandLine) {
                // Preserve the long-standing CLI diagnostic consumed by the
                // lifecycle/operations probes while keeping property errors
                // distinguishable from command-line errors.
                std::fprintf(stderr, "[cppfm] invalid command-line value for --%s: %s\n",
                             diagnostic.key.c_str(), diagnostic.value.c_str());
            } else {
                std::fprintf(stderr, "[cppfm] invalid server.properties value for %s: %s\n",
                             diagnostic.key.c_str(), diagnostic.value.c_str());
            }
            break;
        case ConfigDiagnosticKind::UnsupportedKey:
            if (commandLine) {
                std::fprintf(stderr, "[cppfm] unsupported command-line option: --%s\n",
                             diagnostic.key.c_str());
            } else {
                std::fprintf(stderr, "[cppfm] unsupported configuration key: %s\n",
                             diagnostic.key.c_str());
            }
            break;
        case ConfigDiagnosticKind::MissingValue:
            std::fprintf(stderr, "[cppfm] missing value for --%s\n",
                         diagnostic.key.c_str());
            break;
        }
    }
}

int main(int argc, char** argv) {
    // Handle informational flags before RuntimeLayout::prepare(): asking for
    // help or a version must not create a server directory, acquire a lock, or
    // start the embedded JVM as a side effect.
    std::vector<std::string> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
    for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i] ? argv[i] : "");
    const CommandLineOptions commandLine = parseCommandLine(arguments);
    if (commandLine.showHelp) {
        printUsage(argv[0]);
        return 0;
    }
    if (commandLine.showVersion) {
        printVersion();
        return 0;
    }
    std::filesystem::path serverRoot;
    std::string layoutError;
    if (!RuntimeLayout::prepare(serverRoot, &layoutError)) {
        std::fprintf(stderr, "[cppfm] runtime layout setup failed: %s\n",
                     layoutError.c_str());
        return 1;
    }
    ServerConfig cfg;
    ConfigDiagnostics propertyDiagnostics;
    (void)loadServerProperties(cfg, "server.properties", &propertyDiagnostics);
    ConfigDiagnostics diagnostics = commandLine.diagnostics;
    applyCommandLine(cfg, commandLine, &diagnostics);
    printConfigDiagnostics(propertyDiagnostics, false);
    printConfigDiagnostics(diagnostics, true);
    if (cfg.jvmClassesDir.empty())
        cfg.jvmClassesDir = RuntimeLayout::embeddedClasses(serverRoot).string();
    if (cfg.jvmLibrariesDir.empty())
        cfg.jvmLibrariesDir = (serverRoot / "libraries").string();
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
