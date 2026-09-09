package net.fabricmc.fabric.impl.biome;

import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.registry.RegistryKey;
import net.minecraft.world.biome.Biome;
import net.minecraft.world.biome.source.util.MultiNoiseUtil;

/** Small, thread-safe mirror of custom Nether biome registrations. */
public final class NetherBiomeData {
    private static final Set<RegistryKey<Biome>> BIOMES = ConcurrentHashMap.newKeySet();
    private NetherBiomeData() { }
    public static void addNetherBiome(RegistryKey<Biome> biome, MultiNoiseUtil.NoiseValuePoint point) { if (biome != null) BIOMES.add(biome); }
    public static void addNetherBiome(RegistryKey<Biome> biome, MultiNoiseUtil.NoiseHypercube point) { if (biome != null) BIOMES.add(biome); }
    public static boolean canGenerateInNether(RegistryKey<Biome> biome) {
        if (biome == null) return false;
        return BIOMES.contains(biome) || "minecraft".equals(biome.getValue().getNamespace()) &&
            Set.of("nether_wastes", "soul_sand_valley", "crimson_forest", "warped_forest", "basalt_deltas").contains(biome.getValue().getPath());
    }
}
