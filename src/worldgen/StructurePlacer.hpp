// StructurePlacer: ConfiguredFeature/PlacedFeature + Jigsaw helper (plan6 §2).
// Reads JSON definitions from assets/data/structures/*.json if present, otherwise
// falls back to hardcoded defaults. Provides deterministic placement checks.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include "../core/Json.hpp"

namespace cppfm::worldgen {

struct ConfiguredFeature {
    std::string name;                 // e.g. minecraft:stronghold
    std::string type;                 // feature type
    json::Value config = json::Value::object();
    // jigsaw pieces (name, weight)
    std::vector<std::pair<std::string,int>> pieces;
};

struct PlacedFeature {
    std::string name;                 // same as feature name
    std::string featureName;
    int spacing = 32;
    int separation = 8;
    std::uint64_t salt = 0;
    double frequency = 1.0;
};

class StructurePlacer {
public:
    explicit StructurePlacer(std::uint64_t seed) : seed_(seed) {}
    // Try to load all JSON files from baseDir (e.g. "assets/data/structures").
    // Returns number of features loaded. Falls back to hardcoded if none.
    int load(const std::string& baseDir);
    void ensureDefaults();

    const ConfiguredFeature* getConfigured(const std::string& name) const;
    const PlacedFeature* getPlaced(const std::string& name) const;
    // Deterministic should-place check for a chunk (mirrors vanilla random spread)
    bool shouldPlaceAt(const PlacedFeature& pf, std::int32_t cx, std::int32_t cz) const;
    // Return origin chunk if this chunk is within footprint of a placement
    bool findOrigin(const PlacedFeature& pf, std::int32_t cx, std::int32_t cz,
                    std::int32_t& outOriginCx, std::int32_t& outOriginCz) const;
    const std::unordered_map<std::string, ConfiguredFeature>& allConfigured() const { return configured_; }
    const std::unordered_map<std::string, PlacedFeature>& allPlaced() const { return placed_; }

private:
    std::uint64_t seed_;
    std::unordered_map<std::string, ConfiguredFeature> configured_;
    std::unordered_map<std::string, PlacedFeature> placed_;
    static double hash01(std::uint64_t seed, std::int64_t gx, std::int64_t gz, std::uint64_t salt);
};

} // namespace cppfm::worldgen
