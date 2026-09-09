package net.minecraft.world;

/** Heightmap type names used by spawn restrictions and world generation. */
public final class Heightmap {
    private Heightmap() { }
    public enum Type {
        WORLD_SURFACE_WG,
        WORLD_SURFACE,
        OCEAN_FLOOR_WG,
        OCEAN_FLOOR,
        MOTION_BLOCKING,
        MOTION_BLOCKING_NO_LEAVES
    }
}
