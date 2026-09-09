package net.minecraft.world.gen.chunk;

import net.minecraft.world.biome.source.BiomeSource;

/**
 * Stable 1.21.4 superclass ABI for world-generation mixins.  The native
 * world generator remains authoritative; this class provides the constructor
 * and common accessors that server-side Fabric mods link against.
 */
public abstract class ChunkGenerator {
    protected final BiomeSource biomeSource;

    protected ChunkGenerator(BiomeSource biomeSource) {
        this.biomeSource = biomeSource == null ? new BiomeSource() : biomeSource;
    }

    public BiomeSource getBiomeSource() { return biomeSource; }
}
