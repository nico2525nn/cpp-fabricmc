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
    std::uint8_t dim = 0;            // BiomeDimension (0=overworld,1=nether,2=end,3=special)
};

struct ParameterRange {
    float min = -1, max = 1;
    bool contains(float v) const { return v >= min && v <= max; }
    float width() const { return max - min; }
};
struct NoiseHypercube {
    ParameterRange temperature, humidity, continentalness, erosion, depth, weirdness;
    float offset = 0.f;
};
struct HypercubeEntry { std::string key; NoiseHypercube cube; std::uint8_t dim = 0; };

enum class BiomeDimension : std::uint8_t { Overworld = 0, Nether = 1, End = 2, Special = 3 };

class MultiNoiseBiomeSource {
public:
    explicit MultiNoiseBiomeSource(std::uint64_t seed)
        : noises_(std::make_shared<NoiseRegistry>(seed ^ 0xB10C1A55ULL)),
          seed_(seed) {
        buildDefaultTable();
    }

    // Register an authored biome point.
    void add(const std::string& key, const ClimateParams& p) {
        addDim(key, p, BiomeDimension::Overworld);
    }
    void addDim(const std::string& key, const ClimateParams& p, BiomeDimension dim) {
        entries_.push_back({key, p, static_cast<std::uint8_t>(dim)});
        // also register hypercube ±0.05 width for isosceles parity
        NoiseHypercube cube;
        auto r = [&](double v){ return ParameterRange{float(v-0.05), float(v+0.05)}; };
        cube.temperature = r(p.temperature);
        cube.humidity = r(p.humidity);
        cube.continentalness = r(p.continentalness);
        cube.erosion = r(p.erosion);
        cube.depth = r(p.depth);
        cube.weirdness = r(p.weirdness);
        entriesCube_.push_back({key, cube, static_cast<std::uint8_t>(dim)});
    }
    void addNether(const std::string& key, double t, double h, double c,
                   double e, double d, double w) {
        addDim(key, ClimateParams{t, h, c, e, d, w}, BiomeDimension::Nether);
    }
    void addEnd(const std::string& key, double t, double h, double c,
                double e, double d, double w) {
        addDim(key, ClimateParams{t, h, c, e, d, w}, BiomeDimension::End);
    }
    // the_void: registry presence only (vanilla default for Y<-64 / ungenerated).
    void addSpecial(const std::string& key) {
        addDim(key, ClimateParams{}, BiomeDimension::Special);
    }
    // Test helpers: hypercube direct
    void clear() { entries_.clear(); entriesCube_.clear(); }
    void addCube(const std::string& key, const NoiseHypercube& cube) {
        entriesCube_.push_back({key, cube, 0});
        ClimateParams mid{};
        mid.temperature = (cube.temperature.min + cube.temperature.max) * 0.5;
        mid.humidity = (cube.humidity.min + cube.humidity.max) * 0.5;
        mid.continentalness = (cube.continentalness.min + cube.continentalness.max) * 0.5;
        mid.erosion = (cube.erosion.min + cube.erosion.max) * 0.5;
        mid.depth = (cube.depth.min + cube.depth.max) * 0.5;
        mid.weirdness = (cube.weirdness.min + cube.weirdness.max) * 0.5;
        entries_.push_back({key, mid, 0});
    }
    void addCubePoint(const std::string& key, double t,double h,double c,double e,double d,double w){
        NoiseHypercube cube;
        auto r=[&](double v){ return ParameterRange{float(v-0.05), float(v+0.05)}; };
        cube.temperature=r(t); cube.humidity=r(h); cube.continentalness=r(c);
        cube.erosion=r(e); cube.depth=r(d); cube.weirdness=r(w);
        addCube(key, cube);
    }

    // Sample climate at world coordinates and resolve to a biome key. Overworld only (dim 0) — nether/end entries never leak here.
    const std::string& sample(double x, double y, double z) const {
        ClimateParams c = climateAt(x, y, z);
        return nearestDim(c, static_cast<std::uint8_t>(BiomeDimension::Overworld));
    }
    const std::string& sampleNether(double x, double y, double z) const {
        ClimateParams c = climateAt(x, y, z);
        return nearestDim(c, static_cast<std::uint8_t>(BiomeDimension::Nether));
    }
    const std::string& sampleEnd(double x, double y, double z) const {
        ClimateParams c = climateAt(x, y, z);
        return nearestDim(c, static_cast<std::uint8_t>(BiomeDimension::End));
    }
    bool contains(const std::string& key) const {
        for (const auto& e : entries_)
            if (e.key == key) return true;
        return false;
    }
    std::size_t dimensionEntryCount(BiomeDimension dim) const {
        const auto d = static_cast<std::uint8_t>(dim);
        std::size_t n = 0;
        for (const auto& e : entries_) if (e.dim == d) ++n;
        return n;
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

    static constexpr double kW_T = 1.0, kW_H = 1.0, kW_C = 1.5, kW_E = 1.5, kW_D = 1.0, kW_W = 1.0;
    static double dist2(const ClimateParams& a, const ClimateParams& b) {
        const double t = a.temperature - b.temperature;
        const double h = a.humidity - b.humidity;
        const double c = a.continentalness - b.continentalness;
        const double e = a.erosion - b.erosion;
        const double d = a.depth - b.depth;
        const double w = a.weirdness - b.weirdness;
        if (!std::isfinite(t) || !std::isfinite(h) || !std::isfinite(c) ||
            !std::isfinite(e) || !std::isfinite(d) || !std::isfinite(w)) return 1e300;
        return kW_T*t*t + kW_H*h*h + kW_C*c*c + kW_E*e*e + kW_D*d*d + kW_W*w*w;
    }
    static double isoscelesWeight(const NoiseHypercube& cube, const ClimateParams& p){
        if (!std::isfinite(p.temperature) || !std::isfinite(p.humidity) || !std::isfinite(p.continentalness) ||
            !std::isfinite(p.erosion) || !std::isfinite(p.depth) || !std::isfinite(p.weirdness)) return 1e300;
        auto dist1 = [](const ParameterRange& r, double v, double w)->double {
            if (v < r.min) return w * (r.min - v) / std::max(double(r.width()), 1e-6);
            if (v > r.max) return w * (v - r.max) / std::max(double(r.width()), 1e-6);
            return 0.0;
        };
        double d = 0;
        d += dist1(cube.temperature, p.temperature, kW_T);
        d += dist1(cube.humidity, p.humidity, kW_H);
        d += dist1(cube.continentalness, p.continentalness, kW_C);
        d += dist1(cube.erosion, p.erosion, kW_E);
        d += dist1(cube.depth, p.depth, kW_D);
        d += dist1(cube.weirdness, p.weirdness, kW_W);
        d += cube.offset;
        return d;
    }
    const std::string& sampleByClimate(const ClimateParams& c) const { return nearest(c); }
    const std::string& sampleByClimateDim(const ClimateParams& c, BiomeDimension dim) const {
        return nearestDim(c, static_cast<std::uint8_t>(dim));
    }
    std::size_t biomeEntryCount() const { return entries_.size(); }
    std::size_t hypercubeEntryCount() const { return entriesCube_.size(); }
    const std::vector<HypercubeEntry>& hypercubes() const { return entriesCube_; }

private:
    const std::string& nearest(const ClimateParams& c) const {
        // Prefer hypercube isosceles if available (vanilla SearchTree parity)
        if (!entriesCube_.empty()) {
            const HypercubeEntry* best = nullptr;
            double bestD = 1e300;
            for (const auto& e : entriesCube_) {
                if (e.dim != 0) continue; // overworld-only legacy path
                const double d = isoscelesWeight(e.cube, c);
                if (d < bestD) { bestD = d; best = &e; }
            }
            if (best) return best->key;
        }
        if (entries_.empty()) { static const std::string fallback = "minecraft:plains"; return fallback; }
        const std::string* best = nullptr;
        double bestD = 1e300;
        for (const auto& e : entries_) {
            if (e.dim != 0) continue;
            const double d = dist2(e.target, c);
            if (d < bestD) { bestD = d; best = &e.key; }
        }
        if (best) return *best;
        { static const std::string fallback = "minecraft:plains"; return fallback; }
    }
    const std::string& nearestDim(const ClimateParams& c, std::uint8_t dim) const {
        if (!entriesCube_.empty()) {
            const HypercubeEntry* best = nullptr;
            double bestD = 1e300;
            for (const auto& e : entriesCube_) {
                if (e.dim != dim) continue;
                const double d = isoscelesWeight(e.cube, c);
                if (d < bestD) { bestD = d; best = &e; }
            }
            if (best) return best->key;
        }
        const std::string* best = nullptr;
        double bestD = 1e300;
        for (const auto& e : entries_) {
            if (e.dim != dim) continue;
            const double d = dist2(e.target, c);
            if (d < bestD) { bestD = d; best = &e.key; }
        }
        if (best) return *best;
        // empty dimension subset: fall back to overworld plains (never the_void)
        { static const std::string fallback = "minecraft:plains"; return fallback; }
    }
    void buildDefaultTable();                        // authored points (.cpp)

    std::shared_ptr<NoiseRegistry> noises_;
    std::uint64_t seed_;
    std::vector<BiomeEntry> entries_;
    std::vector<HypercubeEntry> entriesCube_;
};

} // namespace cppfm::worldgen
