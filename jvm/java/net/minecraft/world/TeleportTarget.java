package net.minecraft.world;

import net.minecraft.util.math.Vec3d;

/** Minimal 1.21.4 teleport target carrier used by the server-player ABI. */
public class TeleportTarget {
    public final World world;
    public final Vec3d position;
    public final float yaw;
    public final float pitch;

    public TeleportTarget(World world, Vec3d position, float yaw, float pitch) {
        this.world = world;
        this.position = position == null ? Vec3d.ZERO : position;
        this.yaw = yaw;
        this.pitch = pitch;
    }
}
