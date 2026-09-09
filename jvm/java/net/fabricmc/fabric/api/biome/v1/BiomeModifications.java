package net.fabricmc.fabric.api.biome.v1;

import java.util.Objects;
import java.util.function.Predicate;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.SpawnGroup;
import net.minecraft.registry.RegistryKey;
import net.minecraft.util.Identifier;
import net.minecraft.world.biome.SpawnSettings;
import net.minecraft.world.gen.GenerationStep;
import net.minecraft.world.gen.carver.ConfiguredCarver;
import net.minecraft.world.gen.feature.PlacedFeature;

/** Convenience entry points for common biome modifications. */
public final class BiomeModifications {
    private BiomeModifications() { }
    public static void addFeature(Predicate<BiomeSelectionContext> selector, GenerationStep.Feature step,
            RegistryKey<PlacedFeature> key) {
        Objects.requireNonNull(key, "placedFeatureKey");
        create(key.getValue()).add(ModificationPhase.ADDITIONS, selector,
            context -> context.getGenerationSettings().addFeature(step, key));
    }
    public static void addCarver(Predicate<BiomeSelectionContext> selector,
            RegistryKey<ConfiguredCarver<?>> key) {
        Objects.requireNonNull(key, "configuredCarverKey");
        create(key.getValue()).add(ModificationPhase.ADDITIONS, selector,
            context -> context.getGenerationSettings().addCarver(key));
    }
    public static void addSpawn(Predicate<BiomeSelectionContext> selector, SpawnGroup group,
            EntityType<?> entityType, int weight, int minGroupSize, int maxGroupSize) {
        Objects.requireNonNull(entityType, "entityType");
        create(EntityType.getId(entityType)).add(ModificationPhase.ADDITIONS, selector,
            context -> context.getSpawnSettings().addSpawn(group,
                new SpawnSettings.SpawnEntry(entityType, weight, minGroupSize, maxGroupSize)));
    }
    public static BiomeModification create(Identifier id) { return new BiomeModification(id); }
}
