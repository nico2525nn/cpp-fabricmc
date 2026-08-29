// MultiNoise: climate-parameter biome selection (plan3.md
// "MultiNoiseバイオームのアーキテクチャ").
//
// Each biome is a point in 6-D climate space (temperature, humidity,
// continentalness, erosion, depth, weirdness). The source samples the same
// parameters at a world position via dedicated noises and picks the nearest
// parameter point (vanilla's nearest-neighbour search, FuzzyOffset style
// distance with equal weights).
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "DensityFunction.hpp"

namespace cppfm::worldgen {

struct ClimateParams {
    double temperature = 0, humidity = 0, continentalness = 0,
           erosion = 0, depth = 0, weirdness = 0;
};

struct BiomeEntry {
    std::string key;                 // e.g. minecraft:plains
    ClimateParams target;
};

class MultiNoiseBiomeSource {
public:
    explicit MultiNoiseBiomeSource(std::uint64_t seed)
        : noises_(std::make_shared<NoiseRegistry>(seed ^ 0xB10C1A55ULL)),
          seed_(seed) {
        buildDefaultTable();
    }

    // Register an authored biome point.
    void add(const std::string& key, const ClimateParams& p) {
        entries_.push_back({key, p});
    }

    // Sample climate at world coordinates and resolve to a biome key.
    const std::string& sample(double x, double y, double z) const {
        ClimateParams c = climateAt(x, y, z);
        return nearest(c);
    }

    // Exposed for tests / structure placement rules.
    ClimateParams climateAt(double x, double y, double z) const {
        ClimateParams c;
        c.temperature = noises_->get("temperature").sample(x * 0.0016, 0, z * 0.0016);
        c.humidity    = noises_->get("humidity").sample(x * 0.0016, 0, z * 0.0016);
        c.continentalness = noises_->get("continentalness").octaves(x * 0.0009, 0, z * 0.0009, 3);
        c.erosion     = noises_->get("erosion").octaves(x * 0.0011, 0, z * 0.0011, 3);
        c.weirdness   = noises_->get("weirdness").sample(x * 0.0021, 0, z * 0.0021);
        // depth derives from terrain height estimate: 0 at sea level, +down
        const double h = heightEstimate(x, z);
        c.depth = std::clamp((63.0 - h) / 128.0, -1.0, 1.0);
        return c;
    }

    double heightEstimate(double x, double z) const {
        const double cont = noises_->get("continentalness")
                                .octaves(x * 0.0009, 0, z * 0.0009, 3);
        const double ero  = noises_->get("erosion").octaves(x * 0.0011, 0, z * 0.0011, 3);
        const double peaks= noises_->get("peaks").octaves(x * 0.008, 0, z * 0.008, 4);
        double base = 68.0 + cont * 40.0;
        base -= std::max(0.0, -cont) * 22.0;             // deepen oceans
        base += peaks * 16.0 * std::max(0.25, 0.5 + cont * 0.5);
        base += ero * 6.0;
        return base;
    }

private:
    static constexpr double kW_T = 1.0, kW_H = 1.0, kW_C = 1.5, kW_E = 1.5, kW_D = 1.0, kW_W = 1.0;
    static double dist2(const ClimateParams& a, const ClimateParams& b) {
        const double t = a.temperature - b.temperature;
        const double h = a.humidity - b.humidity;
        const double c = a.continentalness - b.continentalness;
        const double e = a.erosion - b.erosion;
        const double d = a.depth - b.depth;
        const double w = a.weirdness - b.weirdness;
        if (!std::isfinite(t) || !std::isfinite(h) || !std::isfinite(c) || !std::isfinite(e) || !std::isfinite(d) || !std::isfinite(w)) return 1e300;
        return kW_T*t*t + kW_H*h*h + kW_C*c*c + kW_E*e*e + kW_D*d*d + kW_W*w*w;
    }
    const std::string& nearest(const ClimateParams& c) const {
        if (entries_.empty()) { static const std::string fallback = "minecraft:plains"; return fallback; }
        const std::string* best = &entries_.front().key;
        double bestD = 1e300;
        for (const auto& e : entries_) {
            const double d = dist2(e.target, c);
            if (d < bestD) { bestD = d; best = &e.key; }
        }
        return *best;
    }
public:
    // Exposed for testing: nearest by climate without sampling
    const std::string& sampleByClimate(const ClimateParams& c) const { return nearest(c); }
    void buildDefaultTable();                        // authored points (.cpp)

private:
    std::shared_ptr<NoiseRegistry> noises_;
    std::uint64_t seed_;
    std::vector<BiomeEntry> entries_;
};

} // namespace cppfm::worldgen
