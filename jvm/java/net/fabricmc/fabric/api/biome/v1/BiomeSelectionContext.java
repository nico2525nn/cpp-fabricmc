package net.fabricmc.fabric.api.biome.v1;

import java.util.Optional;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.world.biome.Biome;
import net.minecraft.world.dimension.DimensionOptions;
import net.minecraft.world.gen.feature.ConfiguredFeature;
import net.minecraft.world.gen.feature.PlacedFeature;
import net.minecraft.world.gen.structure.Structure;

/** Read-only view passed to biome selectors. */
public interface BiomeSelectionContext {
    RegistryKey<Biome> getBiomeKey();
    Biome getBiome();
    RegistryEntry<Biome> getBiomeRegistryEntry();
    default boolean hasFeature(RegistryKey<ConfiguredFeature<?, ?>> key) {
        for (var step : getBiome().getGenerationSettings().getFeatures())
            for (RegistryEntry<PlacedFeature> placed : step) {
                if (placed.value() != null && placed.value().feature() != null &&
                        key != null && key.equals(placed.value().feature().registryKey())) return true;
            }
        return false;
    }
    default boolean hasPlacedFeature(RegistryKey<PlacedFeature> key) {
        for (var step : getBiome().getGenerationSettings().getFeatures())
            for (RegistryEntry<PlacedFeature> placed : step)
                if (key != null && key.equals(placed.registryKey())) return true;
        return false;
    }
    Optional<RegistryKey<ConfiguredFeature<?, ?>>> getFeatureKey(ConfiguredFeature<?, ?> configuredFeature);
    Optional<RegistryKey<PlacedFeature>> getPlacedFeatureKey(PlacedFeature placedFeature);
    boolean validForStructure(RegistryKey<Structure> key);
    Optional<RegistryKey<Structure>> getStructureKey(Structure structureFeature);
    boolean canGenerateIn(RegistryKey<DimensionOptions> dimensionKey);
    boolean hasTag(TagKey<Biome> tag);
}
