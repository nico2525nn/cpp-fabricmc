package net.fabricmc.fabric.api.biome.v1;

import net.fabricmc.fabric.impl.biome.TheEndBiomeData;
import net.minecraft.registry.RegistryKey;
import net.minecraft.world.biome.Biome;

/** Registration hooks for the default End island regions. */
public final class TheEndBiomes {
    private TheEndBiomes() { }
    public static void addMainIslandBiome(RegistryKey<Biome> biome, double weight) { TheEndBiomeData.addMainIslandBiome(biome, weight); }
    public static void addHighlandsBiome(RegistryKey<Biome> biome, double weight) { TheEndBiomeData.addHighlandsBiome(biome, weight); }
    public static void addSmallIslandsBiome(RegistryKey<Biome> biome, double weight) { TheEndBiomeData.addSmallIslandsBiome(biome, weight); }
    public static void addMidlandsBiome(RegistryKey<Biome> highlands, RegistryKey<Biome> midlands, double weight) { TheEndBiomeData.addMidlandsBiome(highlands, midlands, weight); }
    public static void addBarrensBiome(RegistryKey<Biome> highlands, RegistryKey<Biome> barrens, double weight) { TheEndBiomeData.addBarrensBiome(highlands, barrens, weight); }
}
