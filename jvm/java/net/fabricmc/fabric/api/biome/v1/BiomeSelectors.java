package net.fabricmc.fabric.api.biome.v1;

import java.util.Collection;
import java.util.Set;
import java.util.function.Predicate;
import net.minecraft.entity.EntityType;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.world.biome.Biome;

/** Common selectors for composing biome modifications. */
public final class BiomeSelectors {
    private BiomeSelectors() { }
    public static Predicate<BiomeSelectionContext> all() { return context -> true; }
    public static Predicate<BiomeSelectionContext> vanilla() {
        return context -> context.getBiomeKey() != null &&
            "minecraft".equals(context.getBiomeKey().getValue().getNamespace());
    }
    public static Predicate<BiomeSelectionContext> foundInOverworld() {
        return context -> context.canGenerateIn(RegistryKey.of(net.minecraft.registry.RegistryKeys.DIMENSION,
            net.minecraft.util.Identifier.ofVanilla("overworld")));
    }
    public static Predicate<BiomeSelectionContext> foundInTheNether() {
        return context -> context.canGenerateIn(RegistryKey.of(net.minecraft.registry.RegistryKeys.DIMENSION,
            net.minecraft.util.Identifier.ofVanilla("the_nether")));
    }
    public static Predicate<BiomeSelectionContext> foundInTheEnd() {
        return context -> context.canGenerateIn(RegistryKey.of(net.minecraft.registry.RegistryKeys.DIMENSION,
            net.minecraft.util.Identifier.ofVanilla("the_end")));
    }
    public static Predicate<BiomeSelectionContext> tag(TagKey<Biome> tag) { return context -> context.hasTag(tag); }
    @SafeVarargs public static Predicate<BiomeSelectionContext> excludeByKey(RegistryKey<Biome>... keys) {
        return excludeByKey(Set.of(keys));
    }
    public static Predicate<BiomeSelectionContext> excludeByKey(Collection<RegistryKey<Biome>> keys) {
        return context -> keys == null || !keys.contains(context.getBiomeKey());
    }
    @SafeVarargs public static Predicate<BiomeSelectionContext> includeByKey(RegistryKey<Biome>... keys) {
        return includeByKey(Set.of(keys));
    }
    public static Predicate<BiomeSelectionContext> includeByKey(Collection<RegistryKey<Biome>> keys) {
        return context -> keys != null && keys.contains(context.getBiomeKey());
    }
    public static Predicate<BiomeSelectionContext> spawnsOneOf(EntityType<?>... types) {
        return spawnsOneOf(Set.of(types));
    }
    public static Predicate<BiomeSelectionContext> spawnsOneOf(Set<EntityType<?>> types) {
        return context -> {
            if (types == null) return false;
            for (var group : net.minecraft.entity.SpawnGroup.values())
                for (var entry : context.getBiome().getSpawnSettings().getSpawner(group))
                    if (types.contains(entry.type())) return true;
            return false;
        };
    }
}
