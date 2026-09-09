package net.minecraft.world.biome.source;

/** Marker ABI for the 1.21.4 chunk-biome population callback. */
@FunctionalInterface
public interface BiomeSupplier {
    Object getBiome(int x, int y, int z, Object sampler);
}
