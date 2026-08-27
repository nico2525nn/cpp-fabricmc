// ChunkGenerator implementations: delegate to World private helpers (friend)
#include "ChunkGenerator.hpp"
#include "../game/World.hpp"

namespace cppfm::worldgen {

void FlatLevelSource::fillChunk(Chunk& c, int cx, int cz) const {
    (void)cx; (void)cz;
    world_->fillFlat(c);
}

void NormalLevelSource::fillChunk(Chunk& c, int cx, int cz) const {
    world_->fillTerrainV3(c, cx, cz);
}
void NormalLevelSource::populateNoise(Chunk& c, int cx, int cz) const {
    // noise stage already inside fillTerrainV3; separate hook for future split
    (void)c; (void)cx; (void)cz;
}
void NormalLevelSource::populateStructures(Chunk& c, int cx, int cz) const {
    // structures stage already inside fillTerrainV3 via StructureGenerator
    (void)c; (void)cx; (void)cz;
}

void NetherLevelSource::fillChunk(Chunk& c, int cx, int cz) const {
    world_->fillNether(c, cx, cz);
}

void EndLevelSource::fillChunk(Chunk& c, int cx, int cz) const {
    world_->fillEnd(c, cx, cz);
}

} // namespace cppfm::worldgen
