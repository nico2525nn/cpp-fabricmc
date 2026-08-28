// StructureManager: data-driven structure placement (plan7 World Management)
// Holds StructureSet list and generates structures per chunk via generate()
// Originally in Structures.cpp; now data-driven via JSON.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include "MultiNoise.hpp"
#include "StructurePlacer.hpp"

namespace cppfm { struct Chunk; }

namespace cppfm::worldgen {

// Use distinct names to avoid clash with legacy Structures.hpp which also defines StructureSet
struct SMStructureSet {
    std::string name;            // e.g. minecraft:village
    int spacing = 32;
    int separation = 8;
    std::uint64_t salt = 0;
    std::vector<std::string> biomes; // substring any-of (empty = all)
};

inline double smStructureHash(std::uint64_t seed, std::int64_t gx, std::int64_t gz,
                            std::uint64_t salt) {
    std::uint64_t h = seed ^ salt;
    h ^= static_cast<std::uint64_t>(gx) * 0x9E3779B97F4A7C15ULL;
    h ^= static_cast<std::uint64_t>(gz) * 0xC2B2AE3D27D4EB4FULL;
    h ^= h >> 33; h *= 0xFF51AFD7ED558CCDULL; h ^= h >> 33;
    return (h >> 11) / double(1ULL << 53);
}
inline double structureHash(std::uint64_t seed, std::int64_t gx, std::int64_t gz, std::uint64_t salt) {
    return smStructureHash(seed, gx, gz, salt);
}

struct SMStructureAt {
    bool present = false;
    const SMStructureSet* set = nullptr;
    std::int32_t originCx = 0, originCz = 0;
    std::int32_t originX = 0, originZ = 0;
};

inline SMStructureAt smStructureAtChunk(const SMStructureSet& s, std::uint64_t seed,
                                    std::int32_t cx, std::int32_t cz) {
    const std::int64_t gx = std::floor(double(cx) / s.spacing);
    const std::int64_t gz = std::floor(double(cz) / s.spacing);
    SMStructureAt out;
    out.set = &s;
    for (std::int64_t ox = -1; ox <= 1; ++ox)
        for (std::int64_t oz = -1; oz <= 1; ++oz) {
            const std::int64_t cellX = gx + ox, cellZ = gz + oz;
            const double r1 = smStructureHash(seed, cellX, cellZ, s.salt);
            const double r2 = smStructureHash(seed, cellX, cellZ, s.salt ^ 0x9E37ULL);
            const std::int64_t offX = r1 * (s.spacing - s.separation);
            const std::int64_t offZ = r2 * (s.spacing - s.separation);
            const std::int32_t scx = static_cast<std::int32_t>(cellX * s.spacing + offX);
            const std::int32_t scz = static_cast<std::int32_t>(cellZ * s.spacing + offZ);
            if (cx < scx - 3 || cx > scx + 3) continue;
            if (cz < scz - 3 || cz > scz + 3) continue;
            if (!out.present || (std::abs(scx - cx) + std::abs(scz - cz)) <
                                (std::abs(out.originCx - cx) +
                                 std::abs(out.originCz - cz))) {
                out.present = true;
                out.originCx = scx;
                out.originCz = scz;
                out.originX = scx * 16;
                out.originZ = scz * 16;
            }
        }
    return out;
}
inline SMStructureAt structureAtChunk(const SMStructureSet& s, std::uint64_t seed, std::int32_t cx, std::int32_t cz) {
    return smStructureAtChunk(s, seed, cx, cz);
}

class StructureManager {
public:
    using GroundFn = std::function<std::int32_t(std::int32_t, std::int32_t)>;

    explicit StructureManager(std::uint64_t seed);
    explicit StructureManager(std::uint64_t seed, std::shared_ptr<MultiNoiseBiomeSource> biomes);

    int loadFromDirectory(const std::string& dir);
    int loadFromFile(const std::string& path);
    void ensureDefaults();

    void addSet(SMStructureSet s) { sets_.push_back(std::move(s)); }
    const std::vector<SMStructureSet>& sets() const { return sets_; }
    void clear() { sets_.clear(); }

    void generate(Chunk& chunk, std::int32_t cx, std::int32_t cz, const GroundFn& ground) const;

    StructurePlacer& placer() { return *placer_; }
    const StructurePlacer& placer() const { return *placer_; }

    void setBiomeSource(std::shared_ptr<MultiNoiseBiomeSource> b) { biomes_ = std::move(b); }

private:
    void villagePiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                      std::int32_t originX, std::int32_t originZ,
                      const GroundFn& ground) const;
    void villageJigsaw(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                       std::int32_t ox, std::int32_t oz, int depth,
                       const GroundFn& ground) const;
    void villageHouse(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                      std::int32_t bx, std::int32_t bz, int gy) const;
    void villageFarm(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                     std::int32_t bx, std::int32_t bz, int gy) const;
    void villageChurch(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                       std::int32_t bx, std::int32_t bz, int gy) const;
    void strongholdPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                         std::int32_t originX, std::int32_t originZ,
                         const GroundFn& ground) const;
    void mineshaftPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                        std::int32_t originX, std::int32_t originZ,
                        const GroundFn& ground) const;
    void pyramidPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                      std::int32_t originX, std::int32_t originZ,
                      const GroundFn& ground) const;
    void outpostPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                      std::int32_t originX, std::int32_t originZ,
                      const GroundFn& ground) const;
    void jungleTemplePiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                           std::int32_t originX, std::int32_t originZ,
                           const GroundFn& ground) const;
    void iglooPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                    std::int32_t originX, std::int32_t originZ,
                    const GroundFn& ground) const;
    void swampHutPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                       std::int32_t originX, std::int32_t originZ,
                       const GroundFn& ground) const;
    void monumentPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                       std::int32_t originX, std::int32_t originZ,
                       const GroundFn& ground) const;
    void mansionPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                      std::int32_t originX, std::int32_t originZ,
                      const GroundFn& ground) const;

    std::uint64_t seed_;
    std::shared_ptr<MultiNoiseBiomeSource> biomes_;
    std::vector<SMStructureSet> sets_;
    std::unique_ptr<StructurePlacer> placer_;
};

} // namespace cppfm::worldgen
