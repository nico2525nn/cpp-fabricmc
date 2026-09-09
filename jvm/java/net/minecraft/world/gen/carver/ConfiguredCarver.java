package net.minecraft.world.gen.carver;

/** Generic configured-carver holder used by biome modification descriptors. */
public class ConfiguredCarver<C> {
    private final C config;
    public ConfiguredCarver() { this(null); }
    public ConfiguredCarver(C config) { this.config = config; }
    public C config() { return config; }
}
