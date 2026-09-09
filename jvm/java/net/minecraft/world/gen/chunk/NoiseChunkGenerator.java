package net.minecraft.world.gen.chunk;

import net.minecraft.world.biome.source.BiomeSource;
import net.minecraft.world.chunk.Chunk;

/** 1.21.4 noise-generator target for world-generation mixins. */
public class NoiseChunkGenerator extends ChunkGenerator {
    public NoiseChunkGenerator(BiomeSource biomeSource) { super(biomeSource); }

    protected Chunk populateNoise(Blender blender,
                                  StructureAccessor structureAccessor,
                                  NoiseConfig noiseConfig,
                                  Chunk chunk, int minY, int height) {
        return chunk;
    }
}
