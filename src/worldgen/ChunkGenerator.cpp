// ChunkGenerator implementations: delegate to World private helpers (friend)
#include "ChunkGenerator.hpp"
#include "../game/World.hpp"
#include "../game/Heightmaps.hpp"

namespace cppfm::worldgen {

void FlatLevelSource::fillChunk(ChunkBuildWriter& writer, int cx, int cz) const {
    (void)cx; (void)cz;
    world_->fillFlat(writer);
    captureWorldgenHeightmaps(writer);
}

void NormalLevelSource::fillChunk(ChunkBuildWriter& writer, int cx, int cz) const {
    world_->fillTerrainV3(writer, cx, cz);
}
void NormalLevelSource::populateNoise(ChunkBuildWriter& writer, int cx, int cz) const {
    // noise stage already inside fillTerrainV3; separate hook for future split
    (void)writer; (void)cx; (void)cz;
}
void NormalLevelSource::populateStructures(ChunkBuildWriter& writer, int cx, int cz) const {
    // structures stage already runs inside fillTerrainV3 via StructureManager
    (void)writer; (void)cx; (void)cz;
}

void NetherLevelSource::fillChunk(ChunkBuildWriter& writer, int cx, int cz) const {
    world_->fillNether(writer, cx, cz);
}

void EndLevelSource::fillChunk(ChunkBuildWriter& writer, int cx, int cz) const {
    world_->fillEnd(writer, cx, cz);
}

} // namespace cppfm::worldgen
