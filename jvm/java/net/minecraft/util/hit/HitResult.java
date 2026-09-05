package net.minecraft.util.hit;

import net.minecraft.util.math.Vec3d;

public class HitResult {
    protected final Vec3d pos;
    public HitResult(Vec3d pos) { this.pos = pos == null ? Vec3d.ZERO : pos; }
    public Vec3d getPos() { return pos; }
    public Type getType() { return Type.MISS; }
    public enum Type { MISS, BLOCK, ENTITY }
}
