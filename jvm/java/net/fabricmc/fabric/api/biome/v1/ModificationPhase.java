package net.fabricmc.fabric.api.biome.v1;

/** Ordered phases used to make biome modifier composition deterministic. */
public enum ModificationPhase {
    ADDITIONS,
    REMOVALS,
    REPLACEMENTS,
    POST_PROCESSING
}
