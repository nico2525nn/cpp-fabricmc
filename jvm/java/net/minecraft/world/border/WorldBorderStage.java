package net.minecraft.world.border;

/** The three observable states of a world border in 1.21.4. */
public enum WorldBorderStage {
    STATIONARY(0), GROWING(1), SHRINKING(2);

    private final int color;
    WorldBorderStage(int color) { this.color = color; }
    public int getColor() { return color; }
}
