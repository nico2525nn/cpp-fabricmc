package net.minecraft.world.gen.feature;

/** Typed configured-feature holder used by biome selectors. */
public class ConfiguredFeature<FC, F extends Feature<FC>> {
    private final F feature;
    private final FC config;

    public ConfiguredFeature() { this(null, null); }
    public ConfiguredFeature(F feature, FC config) { this.feature = feature; this.config = config; }
    public F feature() { return feature; }
    public FC config() { return config; }
}
