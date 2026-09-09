package net.minecraft.world.gen;

/** Vanilla feature-generation stages exposed by biome modification APIs. */
public final class GenerationStep {
    private GenerationStep() { }
    public enum Feature {
        RAW_GENERATION,
        LAKES,
        LOCAL_MODIFICATIONS,
        UNDERGROUND_STRUCTURES,
        SURFACE_STRUCTURES,
        STRONGHOLDS,
        UNDERGROUND_ORES,
        UNDERGROUND_DECORATION,
        FLUID_SPRINGS,
        VEGETAL_DECORATION,
        TOP_LAYER_MODIFICATION
    }
}
