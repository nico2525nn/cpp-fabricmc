package net.fabricmc.fabric.api.biome.v1;

import net.minecraft.registry.RegistryKey;
import net.minecraft.world.biome.Biome;
import net.minecraft.world.biome.source.util.MultiNoiseUtil;
import net.fabricmc.fabric.impl.biome.NetherBiomeData;

/** Registration hooks for the default Nether multi-noise source. */
public final class NetherBiomes {
    private NetherBiomes() { }
    public static void addNetherBiome(RegistryKey<Biome> biome, MultiNoiseUtil.NoiseValuePoint point) {
        NetherBiomeData.addNetherBiome(biome, point);
    }
    public static void addNetherBiome(RegistryKey<Biome> biome, MultiNoiseUtil.NoiseHypercube point) {
        NetherBiomeData.addNetherBiome(biome, point);
    }
    public static boolean canGenerateInNether(RegistryKey<Biome> biome) {
        return NetherBiomeData.canGenerateInNether(biome);
    }
}
