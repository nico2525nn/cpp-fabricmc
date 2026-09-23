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
#include <stdexcept>
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
              "view-distance=6\r\n"
              "motd = path with spaces # inline text is data\r\n"
              "empty =\r\n"
              "bare-property\r\n"
              "final-value=kept-without-newline"),
          "properties accept whitespace, CRLF, comments, and final line without newline");
    check(properties.get<int>("view-distance", -1) == 6 &&
              properties.get<int>("VIEW-DISTANCE", -1) == 8 &&
              properties.props.size() == 6,
          "exact duplicate keys use the last value while case variants remain distinct");
    check(properties.parsedEntries().size() == 6 &&
              properties.parsedEntries()[0].first == "VIEW-DISTANCE" &&
              properties.parsedEntries()[1].first == "view-distance",
          "effective entries retain source order by each exact key's last occurrence");
    check(properties.getString("motd") == "path with spaces # inline text is data",
          "values preserve interior spaces and hash characters");
    check(properties.getString("empty", "missing").empty(), "empty values remain empty");
    check(properties.getString("final-value") == "kept-without-newline",
          "last non-newline property is retained");
    check(properties.has("bare-property") && properties.getString("bare-property").empty(),
          "a bare property name is retained with an empty value");

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

void testJavaPropertiesSyntax() {
    std::cout << "\n[Java Properties syntax and escapes]\n";
    struct Fixture {
        std::string_view input;
        std::string_view key;
        std::string_view value;
        const char* description;
    };
    const std::vector<Fixture> fixtures = {
        {"  # hash comment\n ! bang comment\nplain=value", "plain", "value",
         "leading whitespace and both Java comment markers are recognized"},
        {"colon:key:tail", "colon", "key:tail", "colon is a property separator"},
        {"space-key \t = \t value with spaces", "space-key", "value with spaces",
         "whitespace may separate a key and value and is skipped around the separator"},
        {R"(escaped\ key\:\==value)", "escaped key:=", "value",
         "escaped key spaces and delimiters are literal key data"},
        {R"(value=left\=middle\:right\q)", "value", "left=middle:rightq",
         "escaped value separators and Java's unknown-escape rule are applied"},
        {R"(escapes=\t\n\r\f\\)", "escapes", "\t\n\r\f\\",
         "tab, newline, carriage-return, form-feed, and backslash escapes decode"},
        {R"(joined=first\
   second\
	third)", "joined", "firstsecondthird",
         "odd trailing backslashes join physical lines and drop continuation indentation"},
        {R"(continued-eof=value\)", "continued-eof", "value",
         "a continuation marker at EOF is discarded after the accumulated value"},
        {"continued-blank=value\\\n\nnext=value", "continued-blank", "value",
         "an empty continuation line terminates the logical property"},
        {R"(joined-key\
  suffix=value)", "joined-keysuffix", "value",
         "continuation also joins key text while skipping next-line indentation"},
        {"bare-key\n=value\ntrailing=value  ", "bare-key", "",
         "a key without a separator has an empty value"},
        {"trailing=value  ", "trailing", "value  ",
         "trailing value whitespace is preserved"},
        {"=empty-key-value", "", "empty-key-value", "an empty property key is retained"},
        {"inline=hash # and bang ! are data", "inline", "hash # and bang ! are data",
         "comment markers inside values are ordinary data"},
        {"formfeed\f:\fvalue", "formfeed", "value",
         "form-feed is Java Properties whitespace"},
        {"caf\xc3\xa9=\xe9\x9b\xaa", "caf\xc3\xa9", "\xe9\x9b\xaa",
         "valid UTF-8 property text remains valid UTF-8"},
        {R"(rocket=\uD83D\uDE80)", "rocket", "\xf0\x9f\x9a\x80",
         "a valid UTF-16 surrogate pair becomes one UTF-8 code point"},
        {R"(orphan=\uD800)", "orphan", "\xef\xbf\xbd",
         "an isolated UTF-16 surrogate is represented by UTF-8 replacement character"},
    };

    for (const auto& fixture : fixtures) {
        ServerProperties properties;
        const bool loaded = properties.loadText(fixture.input);
        const auto it = properties.props.find(std::string(fixture.key));
        check(loaded && it != properties.props.end() && it->second == fixture.value,
              fixture.description);
    }

    ServerProperties caseSensitive;
    check(caseSensitive.loadText("Name=upper\nname=lower") &&
              caseSensitive.getString("Name") == "upper" &&
              caseSensitive.getString("name") == "lower" &&
              !caseSensitive.has("NAME"),
          "public lookup uses exact Java Properties key spelling");

    ServerProperties preserved;
    (void)preserved.loadText("stable=before");
    int malformedCount = 0;
    for (const std::string_view malformed : {R"(bad=\u12G4)", R"(bad=\u123)"}) {
        bool rejected = false;
        try {
            (void)preserved.loadText(malformed);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        if (rejected && preserved.getString("stable") == "before") ++malformedCount;
    }
    check(malformedCount == 2,
          "malformed Unicode escapes throw and leave the previous property map intact");

    int trueCount = 0;
    for (const std::string_view value : {"true", "TRUE", "TrUe"}) {
        ServerProperties boolean;
        const std::string fixture = "flag=" + std::string(value);
        if (boolean.loadText(fixture) && boolean.get<bool>("flag", false)) ++trueCount;
    }
    int falseCount = 0;
    for (const std::string_view value : {"false", "FALSE", "yes", "1", "on", "true "}) {
        ServerProperties boolean;
        const std::string fixture = "flag=" + std::string(value);
        if (boolean.loadText(fixture) && !boolean.get<bool>("flag", true)) ++falseCount;
    }
    ServerProperties leadingSpaceBoolean;
    const bool leadingSpaceIsFalse = leadingSpaceBoolean.loadText(R"(flag=\ true)") &&
                                     !leadingSpaceBoolean.get<bool>("flag", true);
    ServerProperties missingBoolean;
    check(trueCount == 3 && falseCount == 6 && leadingSpaceIsFalse &&
              missingBoolean.get<bool>("missing", true),
          "boolean getter matches Boolean.parseBoolean; only missing keys use the API default");

    ServerProperties ignoredCommentEscape;
    check(ignoredCommentEscape.loadText("# ignored \\u12G4\nkey=value") &&
              ignoredCommentEscape.getString("key") == "value",
          "malformed escape-looking text in a comment is not decoded");
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
              "enable-rcon=TRUE\n"
              "whitelist=false\n"
              "whitelist=true\n"
              "onlineMode=false\n"
              "onlineMode=false\n"
              "enforces-secure-chat=true\n"
              "compression-threshold=-4\n"
              "maxLoadedChunks=0\n"
              "io-worker-threads=99\n"
              "pvp=false\n"
              "allowFlight=TRUE\n"
              "hardcore=true\n"
              "jvm-enabled=false\n"
              "jvm-strict=TRUE\n"
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
          "configuration accepts canonical case-insensitive Java true/false spellings");
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
              !hasDiagnostic(invalidDiagnostics, ConfigDiagnosticKind::InvalidValue, "difficulty") &&
              invalid.difficulty == "hard",
          "invalid values and supported difficulty remain distinguishable");
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

    const auto utf8Path = base / "utf8.properties";
    {
        std::ofstream output(utf8Path, std::ios::binary);
        output << "motd=caf\xc3\xa9 \xe9\x9b\xaa\n";
    }
    ServerProperties utf8Properties;
    check(utf8Properties.load(utf8Path.string()) &&
              utf8Properties.getString("motd") == "caf\xc3\xa9 \xe9\x9b\xaa",
          "file loading accepts strict UTF-8 and preserves non-ASCII code points");

    const auto latin1Path = base / "latin1.properties";
    {
        std::ofstream output(latin1Path, std::ios::binary);
        std::string fixture = "motd=caf";
        fixture.push_back(static_cast<char>(0xe9));
        fixture.push_back('\n');
        output.write(fixture.data(), static_cast<std::streamsize>(fixture.size()));
    }
    ServerProperties latin1Properties;
    check(latin1Properties.load(latin1Path.string()) &&
              latin1Properties.getString("motd") == "caf\xc3\xa9",
          "invalid UTF-8 file bytes retry as ISO-8859-1 and become valid UTF-8");

    const auto malformedPath = base / "malformed-unicode.properties";
    {
        std::ofstream output(malformedPath, std::ios::binary);
        output << R"(bad=\u12G4)" << '\n';
    }
    ServerProperties malformedFile;
    (void)malformedFile.loadText("stable=before");
    bool malformedFileRejected = false;
    try {
        (void)malformedFile.load(malformedPath.string());
    } catch (const std::invalid_argument&) {
        malformedFileRejected = true;
    }
    check(malformedFileRejected && malformedFile.getString("stable") == "before",
          "malformed Unicode escapes in a file are rejected without partial state changes");

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
    testJavaPropertiesSyntax();
    testSupportedProperties();
    testFileLoadingAndPrecedence();
    testInformationalFlags(argc > 1 ? argv[1] : nullptr);
    std::cout << "\nproperties: " << passed << " PASS " << failed << " FAIL\n";
    return failed == 0 ? 0 : 1;
}
