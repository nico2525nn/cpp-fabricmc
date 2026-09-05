package net.minecraft.entity;

import cppfm.bridge.NativeBridge;
import cppfm.bridge.WrapperCache;
import java.util.UUID;
import net.minecraft.text.Text;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.World;

public class Entity {
    protected final long nativeHandle;
    protected Entity(long nativeHandle) { this.nativeHandle = nativeHandle; }
    public static Entity of(long handle) { return WrapperCache.get(Entity.class, handle, Entity::new); }
    public long nativeHandle() { return nativeHandle; }
    public int getId() { return NativeBridge.nativePlayerEntityId(nativeHandle); }
    public double getX() { return NativeBridge.nativePlayerCoordinate(nativeHandle, 0); }
    public double getY() { return NativeBridge.nativePlayerCoordinate(nativeHandle, 1); }
    public double getZ() { return NativeBridge.nativePlayerCoordinate(nativeHandle, 2); }
    public Vec3d getPos() { return new Vec3d(getX(), getY(), getZ()); }
    public BlockPos getBlockPos() { return new BlockPos((int)Math.floor(getX()), (int)Math.floor(getY()), (int)Math.floor(getZ())); }
    public void setPosition(double x, double y, double z) { NativeBridge.nativePlayerSetPosition(nativeHandle, x, y, z); }
    public void refreshPositionAndAngles(double x, double y, double z, float yaw, float pitch) { setPosition(x, y, z); }
    public UUID getUuid() {
        String value = NativeBridge.nativePlayerUuid(nativeHandle);
        try { return UUID.fromString(value); } catch (RuntimeException ignored) { return new UUID(0L, nativeHandle); }
    }
    public Text getName() { return Text.literal(NativeBridge.nativePlayerName(nativeHandle)); }
    public World getWorld() { return World.of(NativeBridge.nativePlayerWorld(nativeHandle)); }
    public boolean isAlive() { return nativeHandle != 0; }
    public boolean isRemoved() { return false; }
    public void remove(RemovalReason reason) {}
    public enum RemovalReason { KILLED, DISCARDED, UNLOADED_TO_CHUNK, UNLOADED_WITH_PLAYER, CHANGED_DIMENSION }
}
