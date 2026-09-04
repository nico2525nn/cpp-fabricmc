// present, otherwise falls back to hardcoded defaults. Provides deterministic placement checks.
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
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
    std::unordered_map<std::string,std::string> palette; // logical -> minecraft name
    std::unordered_map<std::string,std::unordered_map<std::string,std::string>> variants; // pieceName -> palette override
    std::vector<std::pair<std::string,std::string>> lootByPos; // "x,y,z" -> loot table id
    struct MobPlace{ std::array<int,3> pos{0,0,0}; std::string mob; int count=1; };
    std::vector<MobPlace> mobs;
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
    std::vector<const ConfiguredFeature*> configuredWithType(const std::string& type) const {
        std::vector<const ConfiguredFeature*> out;
        for (auto& [n, cf] : configured_)
            if (cf.type == type || n == type) out.push_back(&cf);
        std::sort(out.begin(), out.end(),
                  [](const ConfiguredFeature* a, const ConfiguredFeature* b){ return a->name < b->name; });
        return out;
    }
    // piece pick. r in [0,1). Falls back to uniform index for empty weights.
    static std::size_t pickWeightedPiece(const ConfiguredFeature& cf, double r) {
        if (cf.pieces.empty()) return 0;
        long total = 0;
        for (auto& p : cf.pieces) total += p.second > 0 ? p.second : 1;
        if (total <= 0) return 0;
        if (r < 0) r = 0;
        if (r >= 1) r = std::nextafter(1.0, 0.0);
        long acc = 0;
        const long target = (long)(r * (double)total);
        for (std::size_t i = 0; i < cf.pieces.size(); ++i) {
            acc += cf.pieces[i].second > 0 ? cf.pieces[i].second : 1;
            if (target < acc) return i;
        }
        return cf.pieces.size() - 1;
    }
    std::uint16_t stateFor(const ConfiguredFeature& cf, const std::string& piece, const std::string& key, const std::string& fallback) const;

private:
    std::uint64_t seed_;
    std::unordered_map<std::string, ConfiguredFeature> configured_;
    std::unordered_map<std::string, PlacedFeature> placed_;
    static double hash01(std::uint64_t seed, std::int64_t gx, std::int64_t gz, std::uint64_t salt);
};

} // namespace cppfm::worldgen
