package net.fabricmc.fabric.impl.biome;

import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.registry.RegistryKey;
import net.minecraft.world.biome.Biome;

/** Small, thread-safe mirror of custom End biome registrations. */
public final class TheEndBiomeData {
    private static final Set<RegistryKey<Biome>> BIOMES = ConcurrentHashMap.newKeySet();
    private TheEndBiomeData() { }
    public static void addMainIslandBiome(RegistryKey<Biome> biome, double weight) { add(biome, weight); }
    public static void addHighlandsBiome(RegistryKey<Biome> biome, double weight) { add(biome, weight); }
    public static void addSmallIslandsBiome(RegistryKey<Biome> biome, double weight) { add(biome, weight); }
    public static void addMidlandsBiome(RegistryKey<Biome> highlands, RegistryKey<Biome> midlands, double weight) { add(midlands, weight); }
    public static void addBarrensBiome(RegistryKey<Biome> highlands, RegistryKey<Biome> barrens, double weight) { add(barrens, weight); }
    private static void add(RegistryKey<Biome> biome, double weight) { if (biome != null && weight > 0.0) BIOMES.add(biome); }
    public static boolean canGenerateInEnd(RegistryKey<Biome> biome) {
        if (biome == null) return false;
        return BIOMES.contains(biome) || "minecraft".equals(biome.getValue().getNamespace()) &&
            Set.of("the_end", "end_highlands", "end_midlands", "small_end_islands", "end_barrens").contains(biome.getValue().getPath());
    }
}
