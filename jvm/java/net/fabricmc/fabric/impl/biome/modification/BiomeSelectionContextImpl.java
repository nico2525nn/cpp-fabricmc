package net.fabricmc.fabric.impl.biome.modification;

import java.util.Optional;
import net.fabricmc.fabric.api.biome.v1.BiomeSelectionContext;
import net.fabricmc.fabric.impl.biome.NetherBiomeData;
import net.fabricmc.fabric.impl.biome.TheEndBiomeData;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.world.biome.Biome;
import net.minecraft.world.dimension.DimensionOptions;
import net.minecraft.world.gen.feature.ConfiguredFeature;
import net.minecraft.world.gen.feature.PlacedFeature;
import net.minecraft.world.gen.structure.Structure;

/** Default selection context backed by the Java shadow registry/value model. */
public final class BiomeSelectionContextImpl implements BiomeSelectionContext {
    private final RegistryKey<Biome> key;
    private final Biome biome;
    private final RegistryEntry<Biome> entry;

    public BiomeSelectionContextImpl(RegistryKey<Biome> key, Biome biome) {
        this.key = key;
        this.biome = biome;
        this.entry = RegistryEntry.of(key, biome);
    }
    @Override public RegistryKey<Biome> getBiomeKey() { return key; }
    @Override public Biome getBiome() { return biome; }
    @Override public RegistryEntry<Biome> getBiomeRegistryEntry() { return entry; }
    @Override public Optional<RegistryKey<ConfiguredFeature<?, ?>>> getFeatureKey(ConfiguredFeature<?, ?> feature) {
        if (feature == null) return Optional.empty();
        for (var step : biome.getGenerationSettings().getFeatures())
            for (RegistryEntry<PlacedFeature> placed : step)
                if (placed.value() != null && placed.value().feature() != null &&
                        placed.value().feature().value() == feature)
                    return Optional.ofNullable(placed.value().feature().registryKey());
        return Optional.empty();
    }
    @Override public Optional<RegistryKey<PlacedFeature>> getPlacedFeatureKey(PlacedFeature feature) {
        if (feature == null) return Optional.empty();
        for (var step : biome.getGenerationSettings().getFeatures())
            for (RegistryEntry<PlacedFeature> placed : step)
                if (placed.value() == feature) return Optional.ofNullable(placed.registryKey());
        return Optional.empty();
    }
    @Override public boolean validForStructure(RegistryKey<Structure> key) { return key != null; }
    @Override public Optional<RegistryKey<Structure>> getStructureKey(Structure structure) { return Optional.empty(); }
    @Override public boolean canGenerateIn(RegistryKey<DimensionOptions> dimensionKey) {
        if (dimensionKey == null || dimensionKey.getValue() == null) return false;
        String path = dimensionKey.getValue().getPath();
        if ("the_nether".equals(path)) return NetherBiomeData.canGenerateInNether(key);
        if ("the_end".equals(path)) return TheEndBiomeData.canGenerateInEnd(key);
        return "overworld".equals(path) || "overworld".equals(dimensionKey.getValue().toString());
    }
    @Override public boolean hasTag(TagKey<Biome> tag) {
        return net.minecraft.registry.Registry.containsTag(tag, biome);
    }
}
