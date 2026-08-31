// Structures: deterministic structure placement + village generation
// (plan3.md "構造物（村など）の配置アーキテクチャ").
//
// Placement mirrors vanilla's random-spread layout: for each structure a grid
// of `spacing` chunks with `separation` jitter, selected by a salted hash so
// every chunk independently agrees whether the structure starts nearby and
// where its pieces extend. Pieces are written chunk-locally (each chunk fills
// only its own intersection), keeping generation lock-free and order-free.
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

struct StructureSet {
    const char* name;          // e.g. minecraft:village_plains
    int spacing;               // chunk-grid period
    int separation;            // min distance between attempts
    std::uint64_t salt;
    // biome gate: substring any-of (empty = all)
    std::vector<const char*> biomes;
};

inline const std::vector<StructureSet>& structureSets() {
    static const std::vector<StructureSet> sets = {
        {"minecraft:village",        34, 8, 0x5A17C, {"plains","savanna","desert","taiga","snowy"}},
        {"minecraft:pillager_outpost",32, 8, 0x0F31, {}},
        {"minecraft:desert_pyramid", 28, 8, 0x2B1E, {"desert"}},
        {"minecraft:jungle_temple",  26, 8, 0x11AA, {"jungle"}},
        {"minecraft:igloo",          30, 8, 0x19D1, {"snowy_plains","snowy_taiga","grove"}},
        {"minecraft:swamp_hut",      26, 8, 0x1C9F, {"swamp"}},
        {"minecraft:mineshaft",      10, 5, 0, {}},
        {"minecraft:monument",       32, 5, 10387313ULL, {"deep_ocean","deep_cold_ocean","deep_frozen_ocean"}},
        {"minecraft:mansion",        80,20, 10387319ULL, {"dark_forest","roofed_forest","pale_garden"}},
        {"minecraft:trial_chambers", 34, 12, 942731826ULL, {}},
        {"minecraft:end_city",       20,11, 10387313ULL, {"end_highlands","end_midlands"}},
    };
    return sets;
}

// Deterministic hash in [0,1).
inline double structHash(std::uint64_t seed, std::int64_t gx, std::int64_t gz,
                         std::uint64_t salt) {
    std::uint64_t h = seed ^ salt;
    h ^= static_cast<std::uint64_t>(gx) * 0x9E3779B97F4A7C15ULL;
    h ^= static_cast<std::uint64_t>(gz) * 0xC2B2AE3D27D4EB4FULL;
    h ^= h >> 33; h *= 0xFF51AFD7ED558CCDULL; h ^= h >> 33;
    return (h >> 11) / double(1ULL << 53);
}

// If chunk (cx,cz) intersects a structure whose origin is within range,
// returns true and fills originChunk/originBlock.
struct StructureAt {
    bool present = false;
    const StructureSet* set = nullptr;
    std::int32_t originCx = 0, originCz = 0;
    std::int32_t originX = 0, originZ = 0;
};

inline StructureAt structureAtChunk(const StructureSet& s, std::uint64_t seed,
                                    std::int32_t cx, std::int32_t cz) {
    // grid cell containing this chunk
    const std::int64_t gx = std::floor(double(cx) / s.spacing);
    const std::int64_t gz = std::floor(double(cz) / s.spacing);
    StructureAt out;
    out.set = &s;
    // check this cell plus neighbours (structure pieces can bleed across)
    for (std::int64_t ox = -1; ox <= 1; ++ox)
        for (std::int64_t oz = -1; oz <= 1; ++oz) {
            const std::int64_t cellX = gx + ox, cellZ = gz + oz;
            const double r1 = structHash(seed, cellX, cellZ, s.salt);
            const double r2 = structHash(seed, cellX, cellZ, s.salt ^ 0x9E37ULL);
            const std::int64_t offX =
                r1 * (s.spacing - s.separation);
            const std::int64_t offZ =
                r2 * (s.spacing - s.separation);
            const std::int32_t scx =
                static_cast<std::int32_t>(cellX * s.spacing + offX);
            const std::int32_t scz =
                static_cast<std::int32_t>(cellZ * s.spacing + offZ);
            // village footprint ~ 40x40 blocks => ±3 chunks
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

class StructureGenerator {
public:
    explicit StructureGenerator(std::uint64_t seed) : seed_(seed),
        biomes_(std::make_shared<MultiNoiseBiomeSource>(seed)),
        placer_(std::make_unique<StructurePlacer>(seed)) {
        placer_->load("assets/data/structures");
    }

    using GroundFn = std::function<std::int32_t(std::int32_t, std::int32_t)>;
    // `ground`: world Y of the first solid block top for a column (from the
    // terrain generator), used so structures sit on the surface.
    void generateChunk(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                       const GroundFn& ground);

    // ConfiguredFeature/PlacedFeature access (plan6 §2)
    const StructurePlacer& placer() const { return *placer_; }

private:
    void villagePiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                      std::int32_t originX, std::int32_t originZ,
                      const GroundFn& ground);
    // Jigsaw-like recursive village (plan6 §2)
    void villageJigsaw(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                       std::int32_t ox, std::int32_t oz, int depth,
                       const GroundFn& ground);
    void villageHouse(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                      std::int32_t bx, std::int32_t bz, int gy);
    void villageFarm(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                     std::int32_t bx, std::int32_t bz, int gy);
    void villageChurch(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                       std::int32_t bx, std::int32_t bz, int gy);
    void strongholdPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                         std::int32_t originX, std::int32_t originZ,
                         const GroundFn& ground);
    void mineshaftPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                        std::int32_t originX, std::int32_t originZ,
                        const GroundFn& ground);
    void pyramidPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                      std::int32_t originX, std::int32_t originZ,
                      const GroundFn& ground);
    void outpostPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                      std::int32_t originX, std::int32_t originZ,
                      const GroundFn& ground);
    void jungleTemplePiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                           std::int32_t originX, std::int32_t originZ,
                           const GroundFn& ground);
    void iglooPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                    std::int32_t originX, std::int32_t originZ,
                    const GroundFn& ground);
    void swampHutPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                       std::int32_t originX, std::int32_t originZ,
                       const GroundFn& ground);
    void monumentPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                       std::int32_t originX, std::int32_t originZ,
                       const GroundFn& ground);
    void mansionPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                      std::int32_t originX, std::int32_t originZ,
                      const GroundFn& ground);
    void trialChambersPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                            std::int32_t originX, std::int32_t originZ,
                            const GroundFn& ground);
    void endCityPiece(Chunk& chunk, std::int32_t cx, std::int32_t cz,
                      std::int32_t originX, std::int32_t originZ,
                      const GroundFn& ground);

    std::uint64_t seed_;
    std::shared_ptr<MultiNoiseBiomeSource> biomes_;
    std::unique_ptr<StructurePlacer> placer_;
};

} // namespace cppfm::worldgen
