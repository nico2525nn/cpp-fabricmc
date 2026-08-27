// ChunkGenerator: abstract base for world generation (plan7 World Management)
// Mirrors net.minecraft.world.level.chunk.ChunkGenerator: fills chunk, populates noise/structures
#pragma once
#include <cstdint>
#include <memory>
#include <functional>

namespace cppfm {
struct Chunk;
class World;
namespace worldgen {

class ChunkGenerator {
public:
    virtual ~ChunkGenerator() = default;
    virtual void fillChunk(Chunk& c, int cx, int cz) const = 0;
    virtual void populateNoise(Chunk& c, int cx, int cz) const {}
    virtual void populateStructures(Chunk& c, int cx, int cz) const {}
};

class FlatLevelSource : public ChunkGenerator {
public:
    explicit FlatLevelSource(const World* world) : world_(world) {}
    void fillChunk(Chunk& c, int cx, int cz) const override;
    void populateNoise(Chunk& c, int cx, int cz) const override {}
    void populateStructures(Chunk& c, int cx, int cz) const override {}
private:
    const World* world_;
};

class NormalLevelSource : public ChunkGenerator {
public:
    explicit NormalLevelSource(const World* world) : world_(world) {}
    void fillChunk(Chunk& c, int cx, int cz) const override;
    void populateNoise(Chunk& c, int cx, int cz) const override;
    void populateStructures(Chunk& c, int cx, int cz) const override;
private:
    const World* world_;
};

class NetherLevelSource : public ChunkGenerator {
public:
    explicit NetherLevelSource(const World* world) : world_(world) {}
    void fillChunk(Chunk& c, int cx, int cz) const override;
    void populateNoise(Chunk& c, int cx, int cz) const override {}
    void populateStructures(Chunk& c, int cx, int cz) const override {}
private:
    const World* world_;
};

class EndLevelSource : public ChunkGenerator {
public:
    explicit EndLevelSource(const World* world) : world_(world) {}
    void fillChunk(Chunk& c, int cx, int cz) const override;
    void populateNoise(Chunk& c, int cx, int cz) const override {}
    void populateStructures(Chunk& c, int cx, int cz) const override {}
private:
    const World* world_;
};

} // namespace worldgen
} // namespace cppfm
