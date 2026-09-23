#include "ServerConfig.hpp"

#include "Constants.hpp"
#include "ServerProperties.hpp"
#include "../core/Random.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <utility>

namespace cppfm {
namespace {

enum class ConfigKey {
    Port,
    MaxPlayers,
    ViewDistance,
    SimulationDistance,
    Motd,
    Assets,
    WorldDir,
    LevelType,
    SpawnProtection,
    StartTime,
    RconPort,
    RconPassword,
    EnableRcon,
    Whitelist,
    OnlineMode,
    SecureProfile,
    SecureChat,
    Difficulty,
    Seed,
    ResourcePack,
    ResourcePackSha1,
    ResourcePackForced,
    CompressionThreshold,
    MaxLoadedChunks,
    IoWorkerThreads,
    Pvp,
    AllowFlight,
    Hardcore,
    JvmEnabled,
    JvmStrict,
    JvmClasses,
    JvmMods,
    JvmConfig,
    JvmJavaHome,
    JvmLibrary,
    JvmLibraries,
};

struct KeySpec {
    std::string_view name;
    ConfigKey key;
    bool property;
    bool commandLine;
};

// This is the only application key registry.  In particular, an entry here
// does not imply support for every similarly named vanilla property.  Unknown
// server.properties keys are reported as unsupported by applyServerProperties.
constexpr std::array<KeySpec, 58> kKeySpecs{{
    {"server-port", ConfigKey::Port, true, true},
    {"port", ConfigKey::Port, true, true},
    {"max-players", ConfigKey::MaxPlayers, true, true},
    {"maxPlayers", ConfigKey::MaxPlayers, true, true},
    {"view-distance", ConfigKey::ViewDistance, true, true},
    {"viewDistance", ConfigKey::ViewDistance, true, true},
    {"simulation-distance", ConfigKey::SimulationDistance, true, true},
    {"simulationDistance", ConfigKey::SimulationDistance, true, true},
    {"motd", ConfigKey::Motd, true, true},
    {"assets", ConfigKey::Assets, false, true},
    {"world-dir", ConfigKey::WorldDir, true, true},
    {"worldDir", ConfigKey::WorldDir, true, true},
    {"level-type", ConfigKey::LevelType, true, true},
    {"levelType", ConfigKey::LevelType, true, true},
    {"spawn-protection", ConfigKey::SpawnProtection, true, true},
    {"spawnProtection", ConfigKey::SpawnProtection, true, true},
    {"start-time", ConfigKey::StartTime, true, true},
    {"startTime", ConfigKey::StartTime, true, true},
    {"rcon.port", ConfigKey::RconPort, true, true},
    {"rcon.password", ConfigKey::RconPassword, true, true},
    {"enable-rcon", ConfigKey::EnableRcon, true, true},
    {"white-list", ConfigKey::Whitelist, true, true},
    {"whitelist", ConfigKey::Whitelist, true, true}, // explicit legacy alias
    {"online-mode", ConfigKey::OnlineMode, true, true},
    {"onlineMode", ConfigKey::OnlineMode, true, true},
    {"enforce-secure-profile", ConfigKey::SecureProfile, true, true},
    {"enforceSecureProfile", ConfigKey::SecureProfile, true, true},
    {"enforcesSecureChat", ConfigKey::SecureChat, true, true},
    {"enforces-secure-chat", ConfigKey::SecureChat, true, true},
    {"difficulty", ConfigKey::Difficulty, true, true},
    {"level-seed", ConfigKey::Seed, true, true},
    {"seed", ConfigKey::Seed, false, true},
    {"resource-pack", ConfigKey::ResourcePack, true, true},
    {"resource-pack-sha1", ConfigKey::ResourcePackSha1, true, true},
    {"require-resource-pack", ConfigKey::ResourcePackForced, true, true},
    {"resourcePack", ConfigKey::ResourcePack, true, true},
    {"resourcePackSha1", ConfigKey::ResourcePackSha1, true, true},
    {"resource-pack-required", ConfigKey::ResourcePackForced, true, true},
    {"requireResourcePack", ConfigKey::ResourcePackForced, true, true},
    {"network-compression-threshold", ConfigKey::CompressionThreshold, true, true},
    {"compression-threshold", ConfigKey::CompressionThreshold, true, true},
    {"max-loaded-chunks", ConfigKey::MaxLoadedChunks, true, true},
    {"maxLoadedChunks", ConfigKey::MaxLoadedChunks, true, true},
    {"io-worker-threads", ConfigKey::IoWorkerThreads, true, true},
    {"pvp", ConfigKey::Pvp, true, true},
    {"allow-flight", ConfigKey::AllowFlight, true, true},
    {"allowFlight", ConfigKey::AllowFlight, true, true},
    {"hardcore", ConfigKey::Hardcore, true, true},
    {"jvm", ConfigKey::JvmEnabled, true, true},
    {"jvm-enabled", ConfigKey::JvmEnabled, true, true},
    {"jvm-strict", ConfigKey::JvmStrict, true, true},
    {"jvm-classes", ConfigKey::JvmClasses, true, true},
    {"jvm-mods", ConfigKey::JvmMods, true, true},
    {"jvm-config", ConfigKey::JvmConfig, true, true},
    {"jvm-java-home", ConfigKey::JvmJavaHome, true, true},
    {"jvm-library", ConfigKey::JvmLibrary, true, true},
    {"jvm-libraries", ConfigKey::JvmLibraries, true, true},
}};

std::string asciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

const KeySpec* findKey(std::string_view name, bool propertySource) {
    const std::string lowered = propertySource ? std::string{} : asciiLower(std::string(name));
    for (const auto& spec : kKeySpecs) {
        if (!(propertySource ? spec.property : spec.commandLine)) continue;
        // java.util.Properties keys are case-sensitive. CLI names have their
        // own, deliberately more permissive matching contract.
        if ((propertySource && spec.name == name) ||
            (!propertySource && asciiLower(std::string(spec.name)) == lowered)) {
            return &spec;
        }
    }
    return nullptr;
}

template<typename T>
bool parseInteger(std::string_view text, T& value) {
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parsePropertyBool(std::string_view text, bool& value) {
    // Vanilla's boolean properties use Boolean.parseBoolean semantics: only
    // the string "true" (case-insensitive) becomes true; all other strings
    // become false rather than being interpreted as CLI-style synonyms.
    value = asciiLower(std::string(text)) == "true";
    return true;
}

bool parseCliBool(std::string_view text, bool& value) {
    const std::string lowered = asciiLower(std::string(text));
    if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on") {
        value = true;
        return true;
    }
    if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off") {
        value = false;
        return true;
    }
    return false;
}

bool parseBool(std::string_view text, bool& value, bool propertySource) {
    return propertySource ? parsePropertyBool(text, value)
                          : parseCliBool(text, value);
}

bool parsePort(std::string_view text, std::uint16_t& port) {
    std::uint32_t parsed = 0;
    if (!parseInteger(text, parsed) || parsed > 65535U) return false;
    port = static_cast<std::uint16_t>(parsed);
    return true;
}

bool parseLevelType(std::string_view text, std::string& levelType) {
    std::string normalized = asciiLower(std::string(text));
    if (normalized.rfind("minecraft:", 0) == 0) normalized.erase(0, 10);
    if (normalized != "normal" && normalized != "flat") return false;
    levelType = std::move(normalized);
    return true;
}

bool parseDifficulty(std::string_view text, std::string& difficulty) {
    const std::string normalized = asciiLower(std::string(text));
    if (normalized != "peaceful" && normalized != "easy" &&
        normalized != "normal" && normalized != "hard") return false;
    difficulty = normalized;
    return true;
}

void applySeed(ServerConfig& config, std::string_view value,
               ConfigDiagnostics* diagnostics, std::string_view originalKey) {
    if (value.empty()) {
        const auto generated = server_config_detail::randomWorldSeed();
        config.seed = generated;
        config.hashedSeed = static_cast<std::int64_t>(generated);
        return;
    }

    std::int64_t numeric = 0;
    const auto numericText = !value.empty() && value.front() == '+'
        ? value.substr(1) : value;
    if (!numericText.empty() && parseInteger(numericText, numeric)) {
        config.hashedSeed = numeric;
        config.seed = static_cast<std::uint64_t>(numeric);
        return;
    }
    const auto hash = rng_detail::javaStringHash(value);
    config.hashedSeed = hash;
    config.seed = static_cast<std::uint64_t>(static_cast<std::int64_t>(hash));
    (void)diagnostics;
    (void)originalKey;
}

void invalid(ConfigDiagnostics* diagnostics, std::string_view key, std::string_view value) {
    if (diagnostics) diagnostics->invalid(std::string(key), std::string(value));
}

void applyValue(ServerConfig& config, ConfigKey key, std::string_view value,
                bool propertySource, ConfigDiagnostics* diagnostics,
                std::string_view originalKey) {
    int intValue = 0;
    std::int64_t longValue = 0;
    bool boolValue = false;
    switch (key) {
    case ConfigKey::Port:
        if (!parsePort(value, config.port)) invalid(diagnostics, originalKey, value);
        break;
    case ConfigKey::MaxPlayers:
        if (!parseInteger(value, intValue)) invalid(diagnostics, originalKey, value);
        else config.maxPlayers = std::max(0, intValue);
        break;
    case ConfigKey::ViewDistance:
        if (!parseInteger(value, intValue)) invalid(diagnostics, originalKey, value);
        else config.viewDistance = std::clamp(intValue, constants::kViewDistanceMin,
                                              constants::kViewDistanceMax);
        break;
    case ConfigKey::SimulationDistance:
        if (!parseInteger(value, intValue)) invalid(diagnostics, originalKey, value);
        else config.simulationDistance = std::clamp(intValue, 2, 32);
        break;
    case ConfigKey::Motd:
        config.motd = std::string(value);
        break;
    case ConfigKey::Assets:
        config.assetsDir = std::string(value);
        break;
    case ConfigKey::WorldDir:
        config.worldDir = std::string(value);
        break;
    case ConfigKey::LevelType:
        if (!parseLevelType(value, config.levelType)) invalid(diagnostics, originalKey, value);
        break;
    case ConfigKey::SpawnProtection:
        if (!parseInteger(value, intValue)) invalid(diagnostics, originalKey, value);
        else config.spawnProtection = std::max(0, intValue);
        break;
    case ConfigKey::StartTime:
        if (!parseInteger(value, longValue)) invalid(diagnostics, originalKey, value);
        else config.startTime = longValue;
        break;
    case ConfigKey::RconPort:
        if (!parsePort(value, config.rcon.port)) invalid(diagnostics, originalKey, value);
        break;
    case ConfigKey::RconPassword:
        config.rcon.password = std::string(value);
        break;
    case ConfigKey::EnableRcon:
        if (!parseBool(value, boolValue, propertySource)) invalid(diagnostics, originalKey, value);
        else config.rcon.enabled = boolValue;
        break;
    case ConfigKey::Whitelist:
        if (!parseBool(value, boolValue, propertySource)) invalid(diagnostics, originalKey, value);
        else config.whitelist = boolValue;
        break;
    case ConfigKey::OnlineMode:
        if (!parseBool(value, boolValue, propertySource)) invalid(diagnostics, originalKey, value);
        else config.onlineMode = boolValue;
        break;
    case ConfigKey::SecureProfile:
        if (!parseBool(value, boolValue, propertySource)) invalid(diagnostics, originalKey, value);
        else config.enforceSecureProfile = boolValue;
        break;
    case ConfigKey::SecureChat:
        if (!parseBool(value, boolValue, propertySource)) invalid(diagnostics, originalKey, value);
        else config.enforcesSecureChat = boolValue;
        break;
    case ConfigKey::Difficulty:
        if (!parseDifficulty(value, config.difficulty)) invalid(diagnostics, originalKey, value);
        break;
    case ConfigKey::Seed:
        applySeed(config, value, diagnostics, originalKey);
        break;
    case ConfigKey::ResourcePack:
        config.resourcePackUrl = std::string(value);
        break;
    case ConfigKey::ResourcePackSha1:
        config.resourcePackSha1 = std::string(value);
        break;
    case ConfigKey::ResourcePackForced:
        if (!parseBool(value, boolValue, propertySource)) invalid(diagnostics, originalKey, value);
        else config.resourcePackForced = boolValue;
        break;
    case ConfigKey::CompressionThreshold:
        if (!parseInteger(value, intValue)) invalid(diagnostics, originalKey, value);
        else config.compressionThreshold = std::max(-1, intValue);
        break;
    case ConfigKey::MaxLoadedChunks:
        if (!parseInteger(value, intValue)) invalid(diagnostics, originalKey, value);
        else config.maxLoadedChunks = std::max(0, intValue);
        break;
    case ConfigKey::IoWorkerThreads:
        if (!parseInteger(value, intValue)) invalid(diagnostics, originalKey, value);
        else config.ioWorkerThreads = std::clamp(intValue, 1, 64);
        break;
    case ConfigKey::Pvp:
        if (!parseBool(value, boolValue, propertySource)) invalid(diagnostics, originalKey, value);
        else config.pvp = boolValue;
        break;
    case ConfigKey::AllowFlight:
        if (!parseBool(value, boolValue, propertySource)) invalid(diagnostics, originalKey, value);
        else config.allowFlight = boolValue;
        break;
    case ConfigKey::Hardcore:
        if (!parseBool(value, boolValue, propertySource)) invalid(diagnostics, originalKey, value);
        else config.hardcore = boolValue;
        break;
    case ConfigKey::JvmEnabled:
        if (!parseBool(value, boolValue, propertySource)) invalid(diagnostics, originalKey, value);
        else config.jvmEnabled = boolValue;
        break;
    case ConfigKey::JvmStrict:
        if (!parseBool(value, boolValue, propertySource)) invalid(diagnostics, originalKey, value);
        else config.jvmStrict = boolValue;
        break;
    case ConfigKey::JvmClasses:
        config.jvmClassesDir = std::string(value);
        break;
    case ConfigKey::JvmMods:
        config.jvmModsDir = std::string(value);
        break;
    case ConfigKey::JvmConfig:
        config.jvmConfigDir = std::string(value);
        break;
    case ConfigKey::JvmJavaHome:
        config.jvmJavaHome = std::string(value);
        break;
    case ConfigKey::JvmLibrary:
        config.jvmLibrary = std::string(value);
        break;
    case ConfigKey::JvmLibraries:
        config.jvmLibrariesDir = std::string(value);
        break;
    }
}

void applyKnownValue(ServerConfig& config, std::string_view key, std::string_view value,
                     bool propertySource, ConfigDiagnostics* diagnostics) {
    const KeySpec* spec = findKey(key, propertySource);
    if (!spec) {
        if (diagnostics) diagnostics->unsupported(std::string(key), std::string(value));
        return;
    }
    applyValue(config, spec->key, value, propertySource, diagnostics, key);
}

} // namespace

std::uint64_t server_config_detail::randomWorldSeed() noexcept {
    try {
        std::random_device rd;
        const auto now = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        return (static_cast<std::uint64_t>(rd()) << 32U) ^
               static_cast<std::uint64_t>(rd()) ^ now;
    } catch (...) {
        return static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }
}

void ConfigDiagnostics::invalid(std::string key, std::string value) {
    entries.push_back({ConfigDiagnosticKind::InvalidValue, std::move(key), std::move(value)});
}

void ConfigDiagnostics::unsupported(std::string key, std::string value) {
    entries.push_back({ConfigDiagnosticKind::UnsupportedKey, std::move(key), std::move(value)});
}

void ConfigDiagnostics::missing(std::string key) {
    entries.push_back({ConfigDiagnosticKind::MissingValue, std::move(key), {}});
}

void applyServerProperties(ServerConfig& config, const ServerProperties& properties,
                           ConfigDiagnostics* diagnostics) {
    // Preserve source order for aliases while still honoring direct edits to
    // the public props map.  parsedEntries contains the final occurrence of
    // every exact key; the current map supplies the value, so a caller that
    // mutates props never applies stale parsed data.
    const bool hasCanonicalWhitelist = properties.props.find("white-list") !=
                                       properties.props.end();
    const auto applyProperty = [&](const std::string& key, const std::string& value) {
        // `whitelist` is a cppfm legacy alias. If vanilla's canonical key is
        // present, it wins independent of source order.
        if (hasCanonicalWhitelist && key == "whitelist") return;
        applyKnownValue(config, key, value, true, diagnostics);
    };
    std::vector<std::string> appliedKeys;
    appliedKeys.reserve(properties.props.size());
    for (const auto& [parsedKey, ignored] : properties.parsedEntries()) {
        const auto current = properties.props.find(parsedKey);
        if (current == properties.props.end()) continue;
        applyProperty(current->first, current->second);
        appliedKeys.push_back(current->first);
    }
    for (const auto& [key, value] : properties.props) {
        if (std::find(appliedKeys.begin(), appliedKeys.end(), key) == appliedKeys.end())
            applyProperty(key, value);
    }

    // The cap is derived only when the user did not specify it.  This keeps the
    // the existing startup default while allowing a properties view distance
    // to size the automatic cap before CLI overrides are applied.
    bool hasMaxLoadedChunks = false;
    for (const auto& entry : properties.props) {
        const KeySpec* spec = findKey(entry.first, true);
        if (spec && spec->key == ConfigKey::MaxLoadedChunks) {
            hasMaxLoadedChunks = true;
            break;
        }
    }
    if (!hasMaxLoadedChunks)
        config.maxLoadedChunks = std::max(8192, config.viewDistance * config.viewDistance * 4);
}

bool loadServerProperties(ServerConfig& config, const std::string& path,
                          ConfigDiagnostics* diagnostics) {
    ServerProperties properties;
    if (!properties.load(path)) return false;
    applyServerProperties(config, properties, diagnostics);
    return true;
}

CommandLineOptions parseCommandLine(const std::vector<std::string>& arguments) {
    CommandLineOptions result;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        if (argument == "--help" || argument == "-h") {
            result.showHelp = true;
            continue;
        }
        if (argument == "--version") {
            result.showVersion = true;
            continue;
        }
        if (!argument.starts_with("--")) continue;

        const std::size_t equals = argument.find('=');
        std::string key = argument.substr(2, equals == std::string::npos
                                               ? std::string::npos : equals - 2);
        if (key.empty()) {
            result.diagnostics.unsupported(key, {});
            continue;
        }
        if (equals != std::string::npos) {
            result.assignments.push_back({std::move(key), argument.substr(equals + 1)});
            continue;
        }
        if (index + 1 >= arguments.size() || arguments[index + 1].starts_with("--")) {
            result.diagnostics.missing(key);
            continue;
        }
        result.assignments.push_back({std::move(key), arguments[++index]});
    }
    return result;
}

void applyCommandLine(ServerConfig& config, const CommandLineOptions& options,
                      ConfigDiagnostics* diagnostics) {
    for (const auto& assignment : options.assignments)
        applyKnownValue(config, assignment.key, assignment.value, false, diagnostics);
}

} // namespace cppfm
