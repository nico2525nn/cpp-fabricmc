package net.minecraft.entity.projectile;

import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityType;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.World;

/** Common projectile base used by Fabric and server-side entity classifiers. */
public class ProjectileEntity extends Entity {
    protected Entity owner;

    protected ProjectileEntity(EntityType<?> type, World world) { super(type, world); }
    protected ProjectileEntity() { super(EntityType.UNKNOWN, null); }

    public Entity getOwner() { return owner; }
    public void setOwner(Entity value) { owner = value; }
    public boolean isOwner(Entity entity) { return owner == entity; }
    public void setVelocity(World world, float pitch, float yaw, float roll, float speed, float divergence) { }
    public void setVelocity(double x, double y, double z, float speed, float divergence) {
        setVelocity(new Vec3d(x, y, z).normalize().multiply(speed));
    }
}
