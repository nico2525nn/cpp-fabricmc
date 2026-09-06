package net.minecraft.world.explosion;

import net.minecraft.entity.Entity;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.World;

/** Minimal server explosion context for behavior hooks. */
public class Explosion {
    private final World world;
    private final Entity entity;
    private final Vec3d position;
    public Explosion() { this(null, null, Vec3d.ZERO); }
    public Explosion(World world, Entity entity, Vec3d position) {
        this.world = world; this.entity = entity; this.position = position == null ? Vec3d.ZERO : position;
    }
    public World getWorld() { return world; }
    public Entity getEntity() { return entity; }
    public Vec3d getPosition() { return position; }

    public enum DestructionType { KEEP, DESTROY, DESTROY_WITH_DECAY, TRIGGER_BLOCK }
}
