package net.minecraft.entity.projectile.thrown;

import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.LivingEntity;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.World;

/** Minimal server-side ender-pearl shell for the 1.21.4 entity ABI. */
public class EnderPearlEntity extends Entity {
    private LivingEntity owner;

    protected EnderPearlEntity(long nativeHandle) { super(nativeHandle); }
    public EnderPearlEntity(EntityType<?> type, World world) { super(type, world); }
    public EnderPearlEntity(World world, LivingEntity owner) {
        super(EntityType.UNKNOWN, world);
        this.owner = owner;
        if (owner != null) setPosition(owner.getX(), owner.getY() + owner.getStandingEyeHeight(), owner.getZ());
    }
    public EnderPearlEntity(World world, double x, double y, double z) {
        super(EntityType.UNKNOWN, world);
        setPosition(x, y, z);
    }
    public LivingEntity getOwner() { return owner; }
    public void setOwner(LivingEntity value) { owner = value; }
    public void setVelocity(LivingEntity owner, float pitch, float yaw, float roll,
                            float speed, float divergence) {
        this.owner = owner;
        double horizontal = Math.cos(Math.toRadians(pitch));
        setVelocity(new Vec3d(-Math.sin(Math.toRadians(yaw)) * horizontal,
                               -Math.sin(Math.toRadians(pitch)),
                               Math.cos(Math.toRadians(yaw)) * horizontal).multiply(speed));
    }
    @Override public void tick() {
        super.tick();
        setPosition(getX() + getVelocity().x, getY() + getVelocity().y, getZ() + getVelocity().z);
    }
}
