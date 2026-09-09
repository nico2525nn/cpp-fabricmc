// Data-driven structure-set loading and deterministic chunk-local generation.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <utility>
#include "MultiNoise.hpp"
#include "StructurePlacer.hpp"

namespace cppfm { struct Chunk; }

namespace cppfm::worldgen {

struct SMStructureSet {
    SMStructureSet() = default;
    SMStructureSet(std::string name_, int spacing_, int separation_, std::uint64_t salt_,
                   std::vector<std::string> biomes_)
        : name(std::move(name_)), spacing(spacing_), separation(separation_), salt(salt_),
          biomes(std::move(biomes_)) {}

    std::string name;            // e.g. minecraft:village
    int spacing = 32;
    int separation = 8;
    std::uint64_t salt = 0;
    std::vector<std::string> biomes; // substring any-of (empty = all)
    enum Spread { Linear, Triangular, Concentric } spread = Linear;
    double frequency = 1.0;
    int locateOffsetX = 0, locateOffsetY = 0, locateOffsetZ = 0;
    int maxHoriz = 3, maxVert = 8;
    std::string exclusionOther; int exclusionCount = 0;
    struct ConcentricInfo { int distance = 32, count = 128, spread = 3; bool enabled = false; } concentric;
    std::vector<std::pair<std::string,int>> structures; // weight for nether_complexes etc
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

inline int triangularOffsetRaw(std::uint64_t seed, std::int64_t gx, std::int64_t gz, std::uint64_t salt, int range){
    if (range <= 0) return 0;
    double r1 = smStructureHash(seed, gx, gz, salt);
    double r2 = smStructureHash(seed, gx, gz, salt ^ 0x9E3779B97F4A7C15ULL);
    int a = int(r1 * range), b = int(r2 * range);
    return (a + b) / 2;
}
// Return the deterministic candidate for one structure-set grid cell.  Keep
// this separate from smStructureAtChunk(): locate can enumerate cells around
// the source directly instead of asking every chunk in a square to repeat the
// same nine-cell neighbourhood calculation.
inline SMStructureAt smStructureAtCell(const SMStructureSet& s,
                                        std::uint64_t seed,
                                        std::int64_t cellX,
                                        std::int64_t cellZ) {
    SMStructureAt out;
    out.set = &s;
    if (s.spacing <= 0) {
        if (s.frequency < 1.0 &&
            smStructureHash(seed, cellX, cellZ, s.salt ^ 0xCAFEBABEULL) > s.frequency)
            return out;
        out.present = true;
        out.originCx = static_cast<std::int32_t>(cellX);
        out.originCz = static_cast<std::int32_t>(cellZ);
        out.originX = out.originCx * 16 + s.locateOffsetX;
        out.originZ = out.originCz * 16 + s.locateOffsetZ;
        return out;
    }

    const int range = std::max(0, s.spacing - s.separation);
    std::int64_t offX = 0, offZ = 0;
    if (s.spread == SMStructureSet::Triangular) {
        offX = triangularOffsetRaw(seed, cellX, cellZ, s.salt, range);
        offZ = triangularOffsetRaw(seed, cellX, cellZ,
                                   s.salt ^ 0xC2B2AE3D27D4EB4FULL, range);
    } else {
        offX = static_cast<std::int64_t>(smStructureHash(seed, cellX, cellZ, s.salt) * range);
        offZ = static_cast<std::int64_t>(smStructureHash(seed, cellX, cellZ,
                                                          s.salt ^ 0x9E37ULL) * range);
    }
    out.originCx = static_cast<std::int32_t>(cellX * s.spacing + offX);
    out.originCz = static_cast<std::int32_t>(cellZ * s.spacing + offZ);
    if (s.frequency < 1.0 &&
        smStructureHash(seed, out.originCx, out.originCz,
                        s.salt ^ 0xCAFEBABEULL) > s.frequency)
        return out;
    out.present = true;
    out.originX = out.originCx * 16 + s.locateOffsetX;
    out.originZ = out.originCz * 16 + s.locateOffsetZ;
    return out;
}

inline SMStructureAt smConcentricStructureAtIndex(const SMStructureSet& s,
                                                   std::uint64_t seed,
                                                   int i) {
    SMStructureAt out;
    out.set = &s;
    if (i < 0 || i >= s.concentric.count) return out;
    const double angle = smStructureHash(seed, i, 0, s.salt) *
                         2.0 * 3.141592653589793;
    const double radiusChunks =
        s.concentric.distance * (1.0 + (i % 8) * 0.5) +
        smStructureHash(seed, i, 2, s.salt ^ 0xCAFE) * s.concentric.spread;
    out.present = true;
    out.originCx = static_cast<std::int32_t>(std::cos(angle) * radiusChunks);
    out.originCz = static_cast<std::int32_t>(std::sin(angle) * radiusChunks);
    out.originX = out.originCx * 16 + s.locateOffsetX;
    out.originZ = out.originCz * 16 + s.locateOffsetZ;
    return out;
}

inline SMStructureAt smStructureAtChunk(const SMStructureSet& s, std::uint64_t seed,
                                    std::int32_t cx, std::int32_t cz) {
    // concentric stronghold: linear approx polar -> skip grid
    if (s.concentric.enabled) {
        SMStructureAt out; out.set = &s;
        for (int i = 0; i < s.concentric.count; ++i) {
            const auto candidate = smConcentricStructureAtIndex(s, seed, i);
            const int scx = candidate.originCx;
            const int scz = candidate.originCz;
            if (cx < scx - s.maxHoriz || cx > scx + s.maxHoriz) continue;
            if (cz < scz - s.maxHoriz || cz > scz + s.maxHoriz) continue;
            if (!out.present || (std::abs(scx - cx) + std::abs(scz - cz)) <
                                (std::abs(out.originCx - cx) + std::abs(out.originCz - cz))) {
                out.present = true;
                out.originCx = scx; out.originCz = scz;
                out.originX = scx * 16 + s.locateOffsetX; out.originZ = scz * 16 + s.locateOffsetZ;
            }
        }
        return out;
    }
    if (s.spacing <= 0) return smStructureAtCell(s, seed, cx, cz);
    const std::int64_t gx = std::floor(double(cx) / s.spacing);
    const std::int64_t gz = std::floor(double(cz) / s.spacing);
    SMStructureAt out;
    out.set = &s;
    for (std::int64_t ox = -1; ox <= 1; ++ox)
        for (std::int64_t oz = -1; oz <= 1; ++oz) {
            const std::int64_t cellX = gx + ox, cellZ = gz + oz;
            const auto candidate = smStructureAtCell(s, seed, cellX, cellZ);
            if (!candidate.present) continue;
            const std::int32_t scx = candidate.originCx;
            const std::int32_t scz = candidate.originCz;
            if (cx < scx - s.maxHoriz || cx > scx + s.maxHoriz) continue;
            if (cz < scz - s.maxHoriz || cz > scz + s.maxHoriz) continue;
            // exclusion zone check: skip if near other set (approx, delegate to caller for precise)
            if (!out.present || (std::abs(scx - cx) + std::abs(scz - cz)) <
                                (std::abs(out.originCx - cx) +
                                 std::abs(out.originCz - cz))) {
                out.present = true;
                out.originCx = scx;
                out.originCz = scz;
                out.originX = scx * 16 + s.locateOffsetX;
                out.originZ = scz * 16 + s.locateOffsetZ;
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

    struct PendingMob { std::array<int,3> pos; std::string mob; int count=1; };
    struct PendingLoot { std::array<int,3> pos; std::string lootTable; };
    void drainPendingMobs(std::vector<PendingMob>& out) const;
    void drainPendingLoot(std::vector<PendingLoot>& out) const;
    std::vector<PendingMob> takePendingMobs() const;
    std::vector<PendingLoot> takePendingLoot() const;
    size_t pendingMobCount() const;
    size_t pendingLootCount() const;
    void clearPending() const;

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
    void trialChambersPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                            std::int32_t originX, std::int32_t originZ,
                            const GroundFn& ground) const;
    void endCityPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                      std::int32_t originX, std::int32_t originZ,
                      const GroundFn& ground) const;
    void ancientCityPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                         std::int32_t originX, std::int32_t originZ,
                         const GroundFn& ground) const;
    void ruinedPortalPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                          std::int32_t originX, std::int32_t originZ,
                          const GroundFn& ground) const;
    void shipwreckPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                       std::int32_t originX, std::int32_t originZ,
                       const GroundFn& ground) const;
    void oceanRuinsPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                        std::int32_t originX, std::int32_t originZ,
                        const GroundFn& ground) const;
    void netherFossilPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                          std::int32_t originX, std::int32_t originZ,
                          const GroundFn& ground) const;
    void buriedTreasurePiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                            std::int32_t originX, std::int32_t originZ,
                            const GroundFn& ground) const;
    void trailRuinsPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                        std::int32_t originX, std::int32_t originZ,
                        const GroundFn& ground) const;

    void placeTrialChambersPalette(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                                   std::int32_t originX, std::int32_t originZ,
                                   const std::string& pieceName,
                                   const std::unordered_map<std::string,std::string>& palette,
                                   int variant, const GroundFn& ground) const;
    void placeGenericPalette(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                             std::int32_t originX, std::int32_t originZ,
                             const std::string& pieceName,
                             const std::unordered_map<std::string,std::string>& palette,
                             int variant, const GroundFn& ground) const;
    void enqueuePendingMob(int x,int y,int z, const std::string& mob, int count) const;
    void enqueuePendingLoot(int x,int y,int z, const std::string& loot) const;

    std::uint64_t seed_;
    std::shared_ptr<MultiNoiseBiomeSource> biomes_;
    std::vector<SMStructureSet> sets_;
    std::unique_ptr<StructurePlacer> placer_;
    mutable std::mutex pendingMtx_;
    mutable std::vector<PendingMob> pendingMobs_;
    mutable std::vector<PendingLoot> pendingLoot_;
};

} // namespace cppfm::worldgen
