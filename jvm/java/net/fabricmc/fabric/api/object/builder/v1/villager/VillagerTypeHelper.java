package net.fabricmc.fabric.api.object.builder.v1.villager;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.registry.RegistryKey;
import net.minecraft.util.Identifier;
import net.minecraft.village.VillagerType;
import net.minecraft.world.biome.Biome;

public final class VillagerTypeHelper {
    private static final Map<Identifier, VillagerType> TYPES = new ConcurrentHashMap<>();
    private static final Map<RegistryKey<Biome>, VillagerType> BIOMES = new ConcurrentHashMap<>();
    private VillagerTypeHelper() { }
    public static VillagerType register(Identifier id) {
        return TYPES.computeIfAbsent(id, value -> VillagerType.register(value, new VillagerType(value.toString())));
    }
    public static void addVillagerTypeToBiome(RegistryKey<Biome> biome, VillagerType type) { if (biome != null && type != null) BIOMES.put(biome, type); }
}
