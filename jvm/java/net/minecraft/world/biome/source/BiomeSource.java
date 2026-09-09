package net.minecraft.world.biome.source;

import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.world.biome.Biome;

/** Native-independent shadow of the 1.21.4 biome-source base type. */
public class BiomeSource {
    public BiomeSource() { }

    public RegistryEntry<Biome> getBiome(int x, int y, int z,
                                         Object sampler) {
        return null;
    }
}
