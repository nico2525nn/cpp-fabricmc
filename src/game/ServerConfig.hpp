#pragma once

#include "../net/RconConfig.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cppfm {

// The default member initializers are the single source of truth for startup
// defaults.  Configuration tests construct this type instead of maintaining a
// second table of expected defaults.
struct ServerConfig {
    std::uint16_t port = 25565;
    std::int32_t maxPlayers = 20;
    std::int32_t viewDistance = 6;
    std::int32_t simulationDistance = 10;
    std::string motd = "CppFabricMC - C++ Minecraft 1.21.4 server";
    std::string worldBiome = "minecraft:plains";
    std::int64_t hashedSeed = 1378645410614731511LL;
    std::string assetsDir = "assets/registry";
    std::string worldDir = "world";
    std::string recipesDir = "assets/data/recipes";
    std::string resourcePackUrl;
    std::string resourcePackSha1;
    bool resourcePackForced = false;
    // Vanilla's server.properties default is the normal terrain generator.
    // Flat worlds remain available through `level-type=flat` or the CLI.
    std::string levelType = "normal";
    bool whitelist = false;
    bool onlineMode = false;
    bool enforcesSecureChat = false;
    RconConfig rcon;
    std::string levelTypeCli;
    std::uint64_t seed = 1378645410614731511ULL;
    std::int64_t startTime = 1000;
    int compressionThreshold = 256;
    int spawnProtection = 16;
    int maxLoadedChunks = 8192;
    int ioWorkerThreads = 4;
    bool pvp = true;
    bool allowFlight = false;
    bool hardcore = false;
    // Fabric-compatible Java integration is enabled by default.  When a JDK
    // or the bundled shadow classes are unavailable, non-strict startup logs
    // the reason and keeps the native server authoritative.
    bool jvmEnabled = true;
    bool jvmStrict = false;
    std::string jvmClassesDir;
    std::string jvmModsDir = "mods";
    std::string jvmConfigDir = "config";
    std::string jvmJavaHome;
    std::string jvmLibrary;
    std::string jvmLibrariesDir;
};

enum class ConfigDiagnosticKind {
    InvalidValue,
    UnsupportedKey,
    MissingValue,
};

struct ConfigDiagnostic {
    ConfigDiagnosticKind kind;
    std::string key;
    std::string value;
};

struct ConfigDiagnostics {
    std::vector<ConfigDiagnostic> entries;

    void invalid(std::string key, std::string value);
    void unsupported(std::string key, std::string value = {});
    void missing(std::string key);
};

struct ConfigAssignment {
    std::string key;
    std::string value;
};

// Parsing is deliberately separate from applying values.  main() can inspect
// help/version before RuntimeLayout::prepare(), while unit tests can exercise
// both --name=value and --name value without creating a server tree.
struct CommandLineOptions {
    bool showHelp = false;
    bool showVersion = false;
    std::vector<ConfigAssignment> assignments;
    ConfigDiagnostics diagnostics;
};

class ServerProperties;

// A missing properties file is not an error: RuntimeLayout creates a template
// and a first launch uses the defaults above.  A false result means the file
// could not be opened or read; malformed values are recorded in diagnostics
// and retain the previous value.
bool loadServerProperties(ServerConfig& config, const std::string& path,
                          ConfigDiagnostics* diagnostics = nullptr);

// Exposed for focused tests and embedding callers that already have a parsed
// properties object.  Unknown keys are retained as explicit diagnostics and
// are not silently treated as implemented vanilla settings.
void applyServerProperties(ServerConfig& config, const ServerProperties& properties,
                           ConfigDiagnostics* diagnostics = nullptr);

CommandLineOptions parseCommandLine(const std::vector<std::string>& arguments);
void applyCommandLine(ServerConfig& config, const CommandLineOptions& options,
                      ConfigDiagnostics* diagnostics = nullptr);

} // namespace cppfm
