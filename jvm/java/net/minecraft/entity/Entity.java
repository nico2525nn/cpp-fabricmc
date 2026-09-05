package net.minecraft.entity;

import cppfm.bridge.WrapperCache;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Set;
import java.util.UUID;
import net.minecraft.server.MinecraftServer;
import net.minecraft.text.Text;
import net.minecraft.util.NativeAccess;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.util.math.ChunkPos;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.World;

/** Handle-backed base entity. A zero handle is a valid inert Java-only entity. */
public class Entity {
    protected final long nativeHandle;
    protected World world;
    protected EntityType<?> type;
    protected double localX, localY, localZ;
    protected Vec3d velocity = Vec3d.ZERO;
    protected float yaw, pitch;
    protected int age;
    private boolean removed;
    private final Set<String> commandTags = new LinkedHashSet<>();

    protected Entity(long nativeHandle) { this(nativeHandle, null, null); }
    protected Entity(long nativeHandle, World world, EntityType<?> type) {
        this.nativeHandle = nativeHandle; this.world = world; this.type = type;
    }
    protected Entity(EntityType<?> type, World world) { this(0L, world, type); }

    public static Entity of(long handle) { return WrapperCache.get(Entity.class, handle, Entity::new); }
    public long nativeHandle() { return nativeHandle; }
    public int getId() { return NativeAccess.playerEntityId(nativeHandle); }
    public double getX() { return nativeHandle == 0 ? localX : NativeAccess.coordinate(nativeHandle, 0); }
    public double getY() { return nativeHandle == 0 ? localY : NativeAccess.coordinate(nativeHandle, 1); }
    public double getZ() { return nativeHandle == 0 ? localZ : NativeAccess.coordinate(nativeHandle, 2); }
    public Vec3d getPos() { return new Vec3d(getX(), getY(), getZ()); }
    public BlockPos getBlockPos() { return BlockPos.ofFloored(getX(), getY(), getZ()); }
    public BlockPos getLandingPos() { return getBlockPos(); }
    public ChunkPos getChunkPos() { return new ChunkPos(getBlockPos()); }

    public void setPosition(double x, double y, double z) {
        localX = x; localY = y; localZ = z; NativeAccess.setPosition(nativeHandle, x, y, z);
    }
    public void refreshPositionAndAngles(double x, double y, double z, float yaw, float pitch) {
        setPosition(x, y, z); this.yaw = yaw; this.pitch = pitch;
    }
    public void refreshPosition() { }
    public void teleport(double x, double y, double z) { setPosition(x, y, z); }
    public void teleport(World targetWorld, double x, double y, double z, float yaw, float pitch) {
        this.world = targetWorld; refreshPositionAndAngles(x, y, z, yaw, pitch);
    }

    public UUID getUuid() {
        String value = NativeAccess.playerUuid(nativeHandle);
        try { return UUID.fromString(value); }
        catch (RuntimeException ignored) {
            return UUID.nameUUIDFromBytes(("cppfm:entity:" + nativeHandle).getBytes(StandardCharsets.UTF_8));
        }
    }
    public Text getName() { return Text.literal(NativeAccess.playerName(nativeHandle)); }
    public Text getDisplayName() { return getName(); }
    public World getWorld() { return world != null ? world : World.of(NativeAccess.playerWorld(nativeHandle)); }
    public World getEntityWorld() { return getWorld(); }
    public MinecraftServer getServer() { World current = getWorld(); return current == null ? null : current.getServer(); }
    public EntityType<?> getType() { return type == null ? EntityType.UNKNOWN : type; }
    public void setWorld(World value) { world = value; }

    public boolean isAlive() { return !removed && (nativeHandle != 0 || type != null); }
    public boolean isRemoved() { return removed; }
    public void remove(RemovalReason reason) { removed = true; }
    public void discard() { remove(RemovalReason.DISCARDED); }
    public boolean isPlayer() { return this instanceof net.minecraft.entity.player.PlayerEntity; }
    public boolean isSneaking() { return false; }
    public boolean isOnGround() { return false; }
    public boolean isTouchingWater() { return false; }
    public boolean isSubmergedInWater() { return false; }
    public boolean canHit() { return isAlive(); }

    public float getYaw() { return yaw; }
    public float getPitch() { return pitch; }
    public void setYaw(float value) { yaw = value; }
    public void setPitch(float value) { pitch = value; }
    public Vec3d getVelocity() { return velocity; }
    public void setVelocity(Vec3d value) { velocity = value == null ? Vec3d.ZERO : value; }
    public void setVelocity(double x, double y, double z) { setVelocity(new Vec3d(x, y, z)); }
    public float getStandingEyeHeight() { return 1.62f; }
    public Vec3d getEyePos() { return getPos().add(0.0, getStandingEyeHeight(), 0.0); }
    public Vec3d getRotationVec(float tickDelta) {
        double yawRadians = Math.toRadians(-yaw) - Math.PI;
        double pitchRadians = Math.toRadians(-pitch);
        double horizontal = Math.cos(pitchRadians);
        return new Vec3d(Math.sin(yawRadians) * horizontal, Math.sin(pitchRadians), Math.cos(yawRadians) * horizontal);
    }
    public Vec3d getRotationVecClient() { return getRotationVec(1.0f); }
    public Box getBoundingBox() {
        float width = getType().getWidth(), height = getType().getHeight();
        return new Box(getX() - width / 2.0, getY(), getZ() - width / 2.0,
                       getX() + width / 2.0, getY() + height, getZ() + width / 2.0);
    }
    public double squaredDistanceTo(Entity other) { return other == null ? Double.POSITIVE_INFINITY : getPos().squaredDistanceTo(other.getPos()); }
    public double squaredDistanceTo(double x, double y, double z) { return getPos().squaredDistanceTo(new Vec3d(x, y, z)); }
    public double distanceTo(Entity other) { return Math.sqrt(squaredDistanceTo(other)); }
    public double distanceTo(Vec3d position) { return getPos().distanceTo(position); }
    public Direction getHorizontalFacing() {
        int index = Math.floorMod(Math.round(yaw / 90.0f), 4);
        return switch (index) { case 0 -> Direction.SOUTH; case 1 -> Direction.WEST; case 2 -> Direction.NORTH; default -> Direction.EAST; };
    }
    public Set<String> getCommandTags() { return Collections.unmodifiableSet(commandTags); }
    public boolean addCommandTag(String tag) { return tag != null && tag.length() <= 256 && commandTags.add(tag); }
    public boolean removeCommandTag(String tag) { return commandTags.remove(tag); }
    public int getAge() { return age; }
    public boolean hasVehicle() { return false; }
    public boolean hasPassengers() { return false; }
    public java.util.List<Entity> getPassengerList() { return java.util.List.of(); }
    public void playSound(Object sound, float volume, float soundPitch) { }

    public enum RemovalReason { KILLED, DISCARDED, UNLOADED_TO_CHUNK, UNLOADED_WITH_PLAYER, CHANGED_DIMENSION }
}
