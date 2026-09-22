#include "../src/game/ServerConfig.hpp"
#include "../src/game/ServerProperties.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>

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

void testCanonicalProperties() {
    std::cout << "\n[canonical server.properties settings]\n";
    ServerProperties properties;
    check(properties.loadText(
              "server-port=25571\n"
              "max-players=17\n"
              "view-distance=12\n"
              "simulation-distance=9\n"
              "motd=matrix server\n"
              "world-dir=matrix world\n"
              "level-type=flat\n"
              "difficulty=hard\n"
              "spawn-protection=32\n"
              "start-time=-123\n"
              "rcon.port=25570\n"
              "rcon.password=matrix-secret\n"
              "enable-rcon=true\n"
              "whitelist=true\n"
              "online-mode=true\n"
              "enforce-secure-profile=true\n"
              "enforces-secure-chat=true\n"
              "network-compression-threshold=128\n"
              "max-loaded-chunks=4096\n"
              "io-worker-threads=8\n"
              "pvp=false\n"
              "allow-flight=true\n"
              "hardcore=true\n"
              "jvm=false\n"
              "jvm-strict=true\n"
              "jvm-classes=classes path\n"
              "jvm-mods=mods path\n"
              "jvm-config=config path\n"
              "jvm-java-home=java path\n"
              "jvm-library=jvm path\n"
              "jvm-libraries=libraries path\n"
              "level-seed=-12345\n"
              "resource-pack=https://example.invalid/pack.zip\n"
              "resource-pack-sha1=0123456789abcdef0123456789abcdef01234567\n"
              "require-resource-pack=true\n"),
          "canonical settings fixture parses");

    ServerConfig config;
    ConfigDiagnostics diagnostics;
    applyServerProperties(config, properties, &diagnostics);
    check(diagnostics.entries.empty(),
          "canonical settings apply without unsupported or invalid diagnostics");
    check(config.port == 25571 && config.maxPlayers == 17 && config.viewDistance == 12 &&
              config.simulationDistance == 9,
          "numeric server settings apply exactly");
    check(config.motd == "matrix server" && config.worldDir == "matrix world" &&
              config.levelType == "flat" && config.difficulty == "hard" &&
              config.startTime == -123,
          "text, difficulty, and level settings preserve their values");
    check(config.spawnProtection == 32 && config.compressionThreshold == 128 &&
              config.maxLoadedChunks == 4096 && config.ioWorkerThreads == 8,
          "bounded operational settings apply exactly");
    check(config.rcon.port == 25570 && config.rcon.password == "matrix-secret" &&
              config.rcon.enabled && config.whitelist && config.onlineMode &&
              config.enforceSecureProfile && config.enforcesSecureChat,
          "RCON, authentication, and profile settings apply");
    check(!config.pvp && config.allowFlight && config.hardcore && !config.jvmEnabled &&
              config.jvmStrict,
          "gameplay and JVM boolean settings apply");
    check(config.jvmClassesDir == "classes path" && config.jvmModsDir == "mods path" &&
              config.jvmConfigDir == "config path" && config.jvmJavaHome == "java path" &&
              config.jvmLibrary == "jvm path" && config.jvmLibrariesDir == "libraries path",
          "all JVM path settings preserve spaces");
    check(config.hashedSeed == -12345 &&
              config.seed == static_cast<std::uint64_t>(static_cast<std::int64_t>(-12345)),
          "numeric level-seed updates both the world seed and login seed");
    ServerProperties textualSeed;
    check(textualSeed.loadText("level-seed=hello\n"),
          "textual level-seed fixture parses");
    ServerConfig textualConfig;
    ConfigDiagnostics textualDiagnostics;
    applyServerProperties(textualConfig, textualSeed, &textualDiagnostics);
    check(textualDiagnostics.entries.empty() && textualConfig.hashedSeed == 99162322 &&
              textualConfig.seed == 99162322,
          "textual level-seed uses Java String.hashCode semantics");
    check(config.resourcePackUrl == "https://example.invalid/pack.zip" &&
              config.resourcePackSha1 == "0123456789abcdef0123456789abcdef01234567" &&
              config.resourcePackForced,
          "resource-pack settings reach the session resource-pack contract");

    ServerProperties independent;
    check(independent.loadText("enforce-secure-profile=true\n"
                              "enforces-secure-chat=false\n"),
          "secure profile/chat independence fixture parses");
    ServerConfig independentConfig;
    ConfigDiagnostics independentDiagnostics;
    applyServerProperties(independentConfig, independent, &independentDiagnostics);
    check(independentDiagnostics.entries.empty() &&
              independentConfig.enforceSecureProfile &&
              !independentConfig.enforcesSecureChat,
          "secure profile enforcement does not silently enforce secure chat");
}

void testAliasOrderAndMapMutation() {
    std::cout << "\n[alias order and mutable property map]\n";
    ServerProperties properties;
    check(properties.loadText("view-distance=4\nviewDistance=8\n"),
          "hyphenated and camel-case aliases parse in source order");
    ServerConfig config;
    ConfigDiagnostics diagnostics;
    applyServerProperties(config, properties, &diagnostics);
    check(config.viewDistance == 8 && diagnostics.entries.empty(),
          "the later alias wins when aliases occur in source order");

    check(properties.loadText("viewDistance=8\nview-distance=4\n"),
          "reversed alias order parses");
    config = ServerConfig{};
    diagnostics.entries.clear();
    applyServerProperties(config, properties, &diagnostics);
    check(config.viewDistance == 4 && diagnostics.entries.empty(),
          "reversing aliases reverses the effective value");

    properties.props["view-distance"] = "9";
    config = ServerConfig{};
    diagnostics.entries.clear();
    applyServerProperties(config, properties, &diagnostics);
    check(config.viewDistance == 9 && diagnostics.entries.empty(),
          "direct edits to the public property map remain effective");
}

void testCommandLinePrecedence() {
    std::cout << "\n[CLI settings and precedence]\n";
    ServerProperties properties;
    check(properties.loadText(
              "level-seed=17\n"
              "resource-pack=file-pack.zip\n"
              "resource-pack-sha1=file-sha\n"
              "require-resource-pack=true\n"),
          "CLI precedence fixture parses");
    ServerConfig config;
    ConfigDiagnostics diagnostics;
    applyServerProperties(config, properties, &diagnostics);

    const CommandLineOptions options = parseCommandLine({
        "--level-seed", "-99",
        "--resource-pack", "cli-pack.zip",
        "--resource-pack-sha1=cli-sha",
        "--require-resource-pack=false",
        "--assets", "cli assets",
        "--jvm-java-home=cli java",
    });
    check(options.diagnostics.entries.empty() && options.assignments.size() == 6,
          "new CLI settings accept both assignment spellings");
    applyCommandLine(config, options, &diagnostics);
    check(diagnostics.entries.empty(), "valid CLI settings produce no diagnostics");
    check(config.hashedSeed == -99 &&
              config.seed == static_cast<std::uint64_t>(static_cast<std::int64_t>(-99)) &&
              config.resourcePackUrl == "cli-pack.zip" &&
              config.resourcePackSha1 == "cli-sha" && !config.resourcePackForced,
          "CLI settings override server.properties values");
    check(config.assetsDir == "cli assets" && config.jvmJavaHome == "cli java",
          "CLI-only paths reach their production config fields");
}

void testInvalidSettings() {
    std::cout << "\n[invalid and unsupported settings]\n";
    ServerConfig config;
    config.port = 4321;
    config.seed = 77;
    config.hashedSeed = 77;
    config.resourcePackForced = true;
    ServerProperties properties;
    check(properties.loadText(
              "server-port=65536\n"
              "level-seed=not-a-seed\n"
              "require-resource-pack=maybe\n"
              "unknown-setting=yes\n"),
          "invalid settings fixture parses");
    ConfigDiagnostics diagnostics;
    applyServerProperties(config, properties, &diagnostics);
    check(config.port == 4321 && config.hashedSeed != 77 && config.resourcePackForced,
          "invalid settings retain prior fields while textual seed remains valid");
    check(hasDiagnostic(diagnostics, ConfigDiagnosticKind::InvalidValue, "server-port") &&
              !hasDiagnostic(diagnostics, ConfigDiagnosticKind::InvalidValue, "level-seed") &&
              hasDiagnostic(diagnostics, ConfigDiagnosticKind::InvalidValue,
                            "require-resource-pack") &&
              hasDiagnostic(diagnostics, ConfigDiagnosticKind::UnsupportedKey,
                            "unknown-setting"),
          "invalid, textual, and unsupported settings remain distinguishable");
}

} // namespace

int main() {
    testCanonicalProperties();
    testAliasOrderAndMapMutation();
    testCommandLinePrecedence();
    testInvalidSettings();
    std::cout << "\nsettings_matrix: " << passed << " PASS " << failed << " FAIL\n";
    return failed == 0 ? 0 : 1;
}
