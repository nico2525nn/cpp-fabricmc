package net.minecraft.entity.vehicle;

import net.minecraft.block.enums.RailShape;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.World;

/** 1.21.4 minecart behavior surface; native physics remains authoritative. */
public class MinecartController {
    protected final AbstractMinecartEntity minecart;

    public MinecartController(AbstractMinecartEntity minecart) { this.minecart = minecart; }

    public void setVelocity(double x, double y, double z) {
        if (minecart != null) minecart.setVelocity(x, y, z);
    }
    public Vec3d limitSpeed(Vec3d velocity) { return velocity == null ? Vec3d.ZERO : velocity; }
    public double getSpeedRetention() { return 0.96D; }
    public boolean handleCollision() { return false; }
    public Direction getHorizontalFacing() { return minecart == null ? Direction.NORTH : minecart.getHorizontalFacing(); }
    public double moveAlongTrack(BlockPos blockPos, RailShape railShape, double remainingMovement) { return remainingMovement; }
    public float getLerpTargetPitch() { return getPitch(); }
    public void setLerpTargetVelocity(double x, double y, double z) { setVelocity(x, y, z); }
    public void setPos(double x, double y, double z) { if (minecart != null) minecart.setPosition(x, y, z); }
    public World getWorld() { return minecart == null ? null : minecart.getWorld(); }
    public double getX() { return minecart == null ? 0.0 : minecart.getX(); }
    public double getY() { return minecart == null ? 0.0 : minecart.getY(); }
    public double getZ() { return minecart == null ? 0.0 : minecart.getZ(); }
    public float getPitch() { return minecart == null ? 0.0f : minecart.getPitch(); }
    public float getYaw() { return minecart == null ? 0.0f : minecart.getYaw(); }
    public void setPos(Vec3d pos) { if (pos != null) setPos(pos.x, pos.y, pos.z); }
    public void tick() { }
    public void resetLerp() { }
    public void moveOnRail(ServerWorld world) { }
    public void setYaw(float yaw) { if (minecart != null) minecart.setYaw(yaw); }
    public void setPitch(float pitch) { if (minecart != null) minecart.setPitch(pitch); }
    public void setPos(double x, double y, double z, float yaw, float pitch, int interpolationSteps) {
        if (minecart != null) minecart.refreshPositionAndAngles(x, y, z, yaw, pitch);
    }
    public double getMaxSpeed(ServerWorld world) { return 0.4D; }
    public Vec3d getPos() { return minecart == null ? Vec3d.ZERO : minecart.getPos(); }
    public Vec3d getVelocity() { return minecart == null ? Vec3d.ZERO : minecart.getVelocity(); }
    public void setVelocity(Vec3d velocity) { if (minecart != null) minecart.setVelocity(velocity); }
    public double getLerpTargetX() { return getX(); }
    public double getLerpTargetY() { return getY(); }
    public double getLerpTargetZ() { return getZ(); }
    public float getLerpTargetYaw() { return getYaw(); }
}
