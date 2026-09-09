package net.minecraft.world.gen.feature;

import net.minecraft.registry.entry.RegistryEntry;

/** Placed-feature wrapper used by biome generation settings. */
public class PlacedFeature {
    private final RegistryEntry<ConfiguredFeature<?, ?>> configuredFeature;
    public PlacedFeature() { this(null); }
    public PlacedFeature(RegistryEntry<ConfiguredFeature<?, ?>> configuredFeature) {
        this.configuredFeature = configuredFeature;
    }
    public RegistryEntry<ConfiguredFeature<?, ?>> feature() { return configuredFeature; }
}
