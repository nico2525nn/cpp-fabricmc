// Focused plan53 Chapter 1 gate.  It tests the production configuration
// module directly and uses the real executable only for informational-flag
// side-effect checks; no defaults are duplicated in this test.

#include "../src/game/ServerConfig.hpp"
#include "../src/game/ServerProperties.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace cppfm;

namespace {

int passed = 0;
int failed = 0;

void check(bool condition, const char* description) {
    if (condition) {
        ++passed;
        std::cout << "  PASS " << description << '\n';
    } else {
        ++failed;
        std::cout << "  FAIL " << description << '\n';
    }
}

bool hasDiagnostic(const ConfigDiagnostics& diagnostics, ConfigDiagnosticKind kind,
                   std::string_view key) {
    for (const auto& diagnostic : diagnostics.entries) {
        if (diagnostic.kind == kind && diagnostic.key == key) return true;
    }
    return false;
}

void testPropertiesSyntax() {
    std::cout << "\n[properties syntax and duplicate keys]\n";
    ServerProperties properties;
    check(properties.loadText(
              "  # comment with CRLF\r\n"
              "\tview-distance = 4\r\n"
              "VIEW-DISTANCE=8\r\n"
              "motd = path with spaces # inline text is data\r\n"
              "empty =\r\n"
              "ignored line without equals\r\n"
              "final-value=kept-without-newline"),
          "properties accept whitespace, CRLF, comments, and final line without newline");
    check(properties.get<int>("view-distance", -1) == 8,
          "duplicate keys and case variants use the last value");
    check(properties.getString("motd") == "path with spaces # inline text is data",
          "values preserve interior spaces and hash characters");
    check(properties.getString("empty", "missing").empty(), "empty values remain empty");
    check(properties.getString("final-value") == "kept-without-newline",
          "last non-newline property is retained");

    check(properties.loadText("replacement=one\nview-distance=9"),
          "a second load replaces the previous property map");
    check(!properties.has("motd") && properties.get<int>("view-distance", -1) == 9 &&
              properties.parsedEntries().size() == 2,
          "repeated loads replace parsedEntries along with props");
    const auto savePath = std::filesystem::temp_directory_path() /
        ("cppfm-properties-save-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    check(properties.save(savePath.string()), "properties save succeeds after a repeated load");
    ServerProperties reloaded;
    check(reloaded.load(savePath.string()) && reloaded.getString("replacement") == "one" &&
              reloaded.get<int>("view-distance", -1) == 9 &&
              reloaded.parsedEntries().size() == 2,
          "saved properties reload with effective entries intact");
    std::filesystem::remove(savePath);
}

void testSupportedProperties() {
    std::cout << "\n[supported properties, aliases, typed fallback, and clamping]\n";
    ServerConfig config;
    const ServerConfig defaults = config;
    ServerProperties properties;
    check(properties.loadText(
              "server-port=0\n"
              "maxPlayers=12\n"
              "viewDistance=99\n"
              "simulation-distance=1\n"
              "motd=server path with spaces\n"
              "spawnProtection=-2\n"
              "level-type=minecraft:flat\n"
              "world-dir=worlds/with spaces\n"
              "start-time=-7\n"
              "rcon.port=0\n"
              "rcon.password=secret\n"
              "enable-rcon=YeS\n"
              "whitelist=on\n"
              "whitelist=true\n"
              "onlineMode=false\n"
              "onlineMode=0\n"
              "enforces-secure-chat=true\n"
              "compression-threshold=-4\n"
              "maxLoadedChunks=0\n"
              "io-worker-threads=99\n"
              "pvp=no\n"
              "allowFlight=YES\n"
              "hardcore=1\n"
              "jvm-enabled=off\n"
              "jvm-strict=ON\n"
              "jvm-mods=mods with spaces\n"
              "jvm-config=config with spaces\n"),
          "supported properties fixture parses");
    ConfigDiagnostics diagnostics;
    applyServerProperties(config, properties, &diagnostics);
    check(config.port == 0 && config.rcon.port == 0,
          "game and RCON port zero are accepted as ephemeral ports");
    check(config.maxPlayers == 12 && config.viewDistance == 32 &&
              config.simulationDistance == 2,
          "integer values apply and bounded distances clamp");
    check(config.motd == "server path with spaces" &&
              config.worldDir == "worlds/with spaces",
          "string settings preserve paths and spaces");
    check(config.spawnProtection == 0 && config.compressionThreshold == -1,
          "negative nonnegative settings clamp to their lower bounds");
    check(config.maxLoadedChunks == 0 && config.ioWorkerThreads == 64,
          "zero max-loaded-chunks remains the explicit unlimited sentinel and worker count clamps");
    check(config.levelType == "flat" && config.rcon.password == "secret" &&
              config.rcon.enabled && config.whitelist && !config.onlineMode &&
              config.enforcesSecureChat,
          "level, RCON, and boolean properties apply through aliases");
    check(!config.pvp && config.allowFlight && config.hardcore &&
              !config.jvmEnabled && config.jvmStrict,
          "boolean aliases accept case-insensitive true/false spellings");
    check(config.jvmModsDir == "mods with spaces" &&
              config.jvmConfigDir == "config with spaces",
          "JVM paths with spaces are not split or normalized");
    check(diagnostics.entries.empty(), "supported settings produce no diagnostics");

    ServerConfig invalid = defaults;
    invalid.port = 43123;
    invalid.viewDistance = 7;
    invalid.levelType = "flat";
    invalid.jvmEnabled = false;
    ServerProperties invalidProperties;
    check(invalidProperties.loadText(
              "server-port=not-a-port\n"
              "view-distance=not-an-int\n"
              "level-type=amplified\n"
              "jvm=maybe\n"
              "difficulty=hard\n"),
          "invalid properties fixture parses");
    ConfigDiagnostics invalidDiagnostics;
    applyServerProperties(invalid, invalidProperties, &invalidDiagnostics);
    check(invalid.port == 43123 && invalid.viewDistance == 7 &&
              invalid.levelType == "flat" && !invalid.jvmEnabled,
          "invalid typed values fall back without changing the prior config");
    check(hasDiagnostic(invalidDiagnostics, ConfigDiagnosticKind::InvalidValue, "server-port") &&
              hasDiagnostic(invalidDiagnostics, ConfigDiagnosticKind::InvalidValue, "view-distance") &&
              hasDiagnostic(invalidDiagnostics, ConfigDiagnosticKind::InvalidValue, "level-type") &&
              hasDiagnostic(invalidDiagnostics, ConfigDiagnosticKind::InvalidValue, "jvm") &&
              hasDiagnostic(invalidDiagnostics, ConfigDiagnosticKind::UnsupportedKey, "difficulty"),
          "invalid values and explicitly unsupported vanilla keys are distinguishable");
}

void testFileLoadingAndPrecedence() {
    std::cout << "\n[file loading and CLI precedence]\n";
    const auto base = std::filesystem::temp_directory_path() /
        ("cppfm-properties-input-" + std::to_string(
#ifndef _WIN32
            static_cast<long long>(::getpid())
#else
            static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count())
#endif
        ));
    std::filesystem::create_directories(base);
    const auto propertiesPath = base / "server properties with spaces.properties";
    {
        std::ofstream output(propertiesPath, std::ios::binary);
        output << "server-port=25570\r\nview-distance=5\r\nmotd=file value\r\n"
                  "world-dir=file world\r\nmax-players=3\r\n";
    }

    ServerConfig config;
    ConfigDiagnostics diagnostics;
    check(loadServerProperties(config, propertiesPath.string(), &diagnostics),
          "properties load accepts a path containing spaces");
    check(config.port == 25570 && config.viewDistance == 5 &&
              config.motd == "file value" && config.worldDir == "file world",
          "file values are loaded before CLI overrides");

    const CommandLineOptions options = parseCommandLine({
        "--port=0", "--view-distance", "32", "--motd", "CLI value with spaces",
        "--world-dir=CLI world with spaces", "--max-players", "9", "--jvm", "YES"});
    check(!options.showHelp && !options.showVersion && options.diagnostics.entries.empty() &&
              options.assignments.size() == 6,
          "both CLI assignment spellings parse without diagnostics");
    applyCommandLine(config, options, &diagnostics);
    check(config.port == 0 && config.viewDistance == 32 && config.maxPlayers == 9 &&
              config.motd == "CLI value with spaces" &&
              config.worldDir == "CLI world with spaces" && config.jvmEnabled,
          "valid CLI values take precedence over server.properties");

    const CommandLineOptions invalidOptions = parseCommandLine({
        "--port", "bad", "--view-distance=99", "--jvm", "maybe", "--unknown", "value",
        "--missing-value"});
    ConfigDiagnostics invalidDiagnostics = invalidOptions.diagnostics;
    applyCommandLine(config, invalidOptions, &invalidDiagnostics);
    check(config.port == 0 && config.viewDistance == 32 && config.jvmEnabled,
          "invalid CLI values fall back while valid out-of-range values clamp");
    ServerConfig propertyFallback;
    ConfigDiagnostics propertyFallbackDiagnostics;
    check(loadServerProperties(propertyFallback, propertiesPath.string(),
                               &propertyFallbackDiagnostics),
          "a fresh config can be reconstructed from the same properties file");
    applyCommandLine(propertyFallback, invalidOptions, &propertyFallbackDiagnostics);
    check(propertyFallback.port == 25570 && propertyFallback.viewDistance == 32 &&
              propertyFallback.jvmEnabled,
          "invalid CLI values retain the properties value while valid CLI values clamp");
    check(hasDiagnostic(invalidDiagnostics, ConfigDiagnosticKind::InvalidValue, "port") &&
              hasDiagnostic(invalidDiagnostics, ConfigDiagnosticKind::InvalidValue, "jvm") &&
              hasDiagnostic(invalidDiagnostics, ConfigDiagnosticKind::UnsupportedKey, "unknown") &&
              hasDiagnostic(invalidDiagnostics, ConfigDiagnosticKind::MissingValue, "missing-value"),
          "CLI invalid, unsupported, and missing-value cases are reported separately");

    const CommandLineOptions informational = parseCommandLine({"--help", "--version"});
    check(informational.showHelp && informational.showVersion,
          "help and version are recognized before any configuration application");
    std::filesystem::remove_all(base);
}

std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (const char character : value) {
        if (character == '\'') quoted += "'\\''";
        else quoted += character;
    }
    quoted += '\'';
    return quoted;
}

void setServerDirectory(const std::string& value) {
#ifdef _WIN32
    (void)_putenv_s("CPPFM_SERVER_DIR", value.c_str());
#else
    (void)setenv("CPPFM_SERVER_DIR", value.c_str(), 1);
#endif
}

void clearServerDirectory() {
#ifdef _WIN32
    (void)_putenv_s("CPPFM_SERVER_DIR", "");
#else
    (void)unsetenv("CPPFM_SERVER_DIR");
#endif
}

void testInformationalFlags(const char* binaryArgument) {
    std::cout << "\n[help/version no-side-effect process gate]\n";
    if (!binaryArgument || std::string(binaryArgument).empty()) {
        check(false, "focused test receives the cppfm binary path");
        return;
    }
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("cppfm-properties-side-effects-" + std::to_string(suffix));
    const auto output = std::filesystem::temp_directory_path() /
        ("cppfm-properties-output-" + std::to_string(suffix) + ".log");
    std::filesystem::create_directories(root);
    setServerDirectory(root.string());
    const std::string binary = shellQuote(binaryArgument);
    const std::string outputPath = shellQuote(output.string());
    const int helpExit = std::system((binary + " --help >" + outputPath + " 2>&1").c_str());
    const bool helpClean = helpExit == 0 && std::filesystem::is_empty(root);
    check(helpClean, "--help exits before creating the server tree or lock");
    const int versionExit = std::system((binary + " --version >" + outputPath + " 2>&1").c_str());
    check(versionExit == 0 && std::filesystem::is_empty(root),
          "--version exits before creating the server tree or lock");
    clearServerDirectory();
    std::filesystem::remove(output);
    std::filesystem::remove_all(root);
}

} // namespace

int main(int argc, char** argv) {
    testPropertiesSyntax();
    testSupportedProperties();
    testFileLoadingAndPrecedence();
    testInformationalFlags(argc > 1 ? argv[1] : nullptr);
    std::cout << "\nproperties: " << passed << " PASS " << failed << " FAIL\n";
    return failed == 0 ? 0 : 1;
}
