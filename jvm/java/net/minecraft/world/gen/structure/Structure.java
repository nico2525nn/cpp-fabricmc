package net.minecraft.world.gen.structure;

import net.minecraft.world.gen.densityfunction.DensityFunction;

/**
 * Minimal 1.21.4 structure ABI.  Structure placement itself is authoritative
 * in the native world generator; the nested records preserve common mod
 * signatures and metadata access.
 */
public class Structure {
    private final Config config;

    public Structure(Config config) {
        this.config = config == null ? new Config() : config;
    }

    public Config config() { return config; }
    public Config getConfig() { return config; }

    public static class Config {
        public Config() { }
    }

    /** Compatibility placeholder for the vanilla structure context type. */
    public static class StructurePosition {
        public StructurePosition() { }
    }

    /** Compatibility placeholder for the vanilla structure result type. */
    public static class StructurePiecesGenerator {
        public StructurePiecesGenerator() { }
    }

    /** Compatibility placeholder for mods that refer to structure density hooks. */
    public interface DensitySampler {
        default double sample(DensityFunction.NoisePos pos) { return 0.0; }
    }
}
