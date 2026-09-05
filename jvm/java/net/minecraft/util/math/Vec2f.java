package net.minecraft.util.math;

import java.util.Objects;

public final class Vec2f {
    public static final Vec2f ZERO = new Vec2f(0.0f, 0.0f);
    public final float x;
    public final float y;

    public Vec2f(float x, float y) { this.x = x; this.y = y; }
    public float getX() { return x; }
    public float getY() { return y; }
    public Vec2f add(float dx, float dy) { return new Vec2f(x + dx, y + dy); }
    public Vec2f multiply(float factor) { return new Vec2f(x * factor, y * factor); }
    @Override public boolean equals(Object other) {
        return other instanceof Vec2f value && Float.compare(x, value.x) == 0 && Float.compare(y, value.y) == 0;
    }
    @Override public int hashCode() { return Objects.hash(x, y); }
    @Override public String toString() { return "Vec2f{" + x + "," + y + "}"; }
}
