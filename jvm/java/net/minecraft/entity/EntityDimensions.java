package net.minecraft.entity;

/** Immutable dimensions used by entity-type builders. */
public final class EntityDimensions {
    private final float width;
    private final float height;
    private final boolean fixed;

    public EntityDimensions(float width, float height, boolean fixed) {
        this.width = width;
        this.height = height;
        this.fixed = fixed;
    }

    public static EntityDimensions fixed(float width, float height) {
        return new EntityDimensions(width, height, true);
    }

    public static EntityDimensions changing(float width, float height) {
        return new EntityDimensions(width, height, false);
    }

    public float width() { return width; }
    public float height() { return height; }
    public boolean fixed() { return fixed; }
    public EntityDimensions scaled(float factor) { return new EntityDimensions(width * factor, height * factor, fixed); }
}
