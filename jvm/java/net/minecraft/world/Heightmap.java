package net.minecraft.world;

/** Heightmap selector names required by the 1.21.4 world ABI. */
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
