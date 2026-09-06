package net.minecraft.entity;

import cppfm.bridge.WrapperCache;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Set;
import java.util.UUID;
import net.minecraft.server.MinecraftServer;
import net.minecraft.block.BlockState;
import net.minecraft.text.Text;
import net.minecraft.util.NativeAccess;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.util.math.ChunkPos;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.World;

/** Handle-backed base entity. A zero handle is a valid inert Java-only entity. */
public class Entity implements net.minecraft.world.entity.EntityLike {
    protected final long nativeHandle;
    protected World world;
    protected EntityType<?> type;
    protected double localX, localY, localZ;
    protected Vec3d velocity = Vec3d.ZERO;
    protected float yaw, pitch;
    protected int age;
    private boolean removed;
    private final Set<String> commandTags = new LinkedHashSet<>();
    /** Fluid-tag cache field retained for Lithium's collection mixin. */
    private Set<net.minecraft.registry.tag.TagKey<?>> field_25599 = new LinkedHashSet<>();
    /** Collision-cache state fields exposed by optional Lithium mixins. */
    private int field_5956;
    private boolean field_54946;
    private boolean field_27857;
    /** Vanilla change-listener slot exposed by Lithium's Entity accessor. */
    private net.minecraft.world.entity.EntityChangeListener changeListener;
    public net.minecraft.world.entity.EntityChangeListener getChangeListener() { return changeListener; }

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
    /** Yarn 1.21.4 alias used by Mojang-mapped mixins. */
    public void setPos(double x, double y, double z) { setPosition(x, y, z); }
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
    public Text getName() {
        String name = NativeAccess.playerName(nativeHandle);
        if (name.isEmpty()) name = getType().getId().toString();
        return Text.literal(name);
    }
    public Text getDisplayName() { return getName(); }
    public World getWorld() { return world != null ? world : World.of(NativeAccess.entityWorld(nativeHandle)); }
    public World getEntityWorld() { return getWorld(); }
    public MinecraftServer getServer() { World current = getWorld(); return current == null ? null : current.getServer(); }
    public EntityType<?> getType() {
        if (type != null) return type;
        return EntityType.byId(NativeAccess.entityType(nativeHandle));
    }
    public void setWorld(World value) { world = value; }

    public boolean isAlive() { return !removed && (nativeHandle == 0 ? type != null : !NativeAccess.entityDead(nativeHandle)); }
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
    public static Vec3d adjustMovementForCollisions(Entity entity, Vec3d movement,
                                                    Box box, World world,
                                                    java.util.List<?> colliders) {
        return movement == null ? Vec3d.ZERO : movement;
    }
    /**
     * Vanilla's collision-list staging hook.  Keeping the list as a local and
     * passing it through this named call gives collision mixins the same
     * observable INVOKE/ModifyVariable boundary as 1.21.4.
     */
    public static java.util.List<?> findCollisionsForMovement(Entity entity, World world,
                                                               java.util.List<?> collisions, Box box) {
        return collisions == null ? java.util.List.of() : collisions;
    }
    /** Suffocation query hook used by block-cache mixins. */
    public boolean isInsideWall() {
        float f = 0.0f;
        Box box = getBoundingBox();
        java.util.stream.Stream<BlockPos> positions = BlockPos.stream(box);
        positions.close();
        return false;
    }
    /** Fluid movement query boundary used by Lithium's section cache. */
    public boolean updateMovementInFluid(net.minecraft.registry.tag.TagKey<?> tag, double speed) {
        Box box = getBoundingBox();
        int x1 = (int) Math.floor(box.minX);
        int x2 = (int) Math.floor(box.maxX);
        int y1 = (int) Math.floor(box.minY);
        int y2 = (int) Math.floor(box.maxY);
        int z1 = (int) Math.floor(box.minZ);
        int z2 = (int) Math.floor(box.maxZ);
        if (isLogicalSideForUpdatingMovement())
            return x1 > x2 || y1 > y2 || z1 > z2 || tag == null || speed < 0.0D;
        return false;
    }
    /** Rebuilds the submerged-fluid cache at the vanilla lifecycle boundary. */
    public void updateSubmergedInWaterState() {
        if (!field_25599.isEmpty()) field_25599.clear();
    }
    public int getBurningDuration() { return Math.max(0, field_5956); }
    public boolean isWet() { return isTouchingWater(); }
    /** Block-contact render tick boundary used by optional collision caching. */
    public void tickBlockCollision(Vec3d lastRenderPos, Vec3d pos) {
        Iterable<BlockState> states = java.util.List.of();
        com.google.common.collect.Iterables.any(states,
            new com.google.common.base.Predicate<BlockState>() {
                @Override public boolean apply(BlockState state) { return state != null && !state.isAir(); }
            });
    }
    /** Yarn 1.21.4 bounding-box helper used by block-contact mixins. */
    private Box calculateDefaultBoundingBox(Vec3d position) {
        Vec3d anchor = position == null ? getPos() : position;
        float width = getType().getWidth(), height = getType().getHeight();
        return new Box(anchor.x - width / 2.0, anchor.y, anchor.z - width / 2.0,
                       anchor.x + width / 2.0, anchor.y + height, anchor.z + width / 2.0);
    }
    /** Block-contact pass boundary exposed for Lithium's optional cache mixin. */
    private void checkBlockCollision(java.util.List<?> movementList,
                                     java.util.Set<?> touchedBlocks) {
        Box box = calculateDefaultBoundingBox(getPos());
        box = box.expand(0.0);
        if (movementList == null || touchedBlocks == null) return;
        if (movementList.isEmpty()) touchedBlocks.isEmpty();
    }
    /** Yarn 1.21.4 instance entrypoint used by collision mixins. */
    public Vec3d adjustMovementForCollisions(Vec3d movement) {
        Box box = getBoundingBox();
        World currentWorld = getWorld();
        java.util.List<?> collisions = currentWorld == null
            ? new java.util.ArrayList<>() : currentWorld.getEntityCollisions(this, box);
        collisions = findCollisionsForMovement(this, currentWorld, collisions, box);
        return adjustMovementForCollisions(this, movement, box, currentWorld, collisions);
    }
    /** Cached feet-state lookup used by collision optimizations. */
    public BlockState getBlockStateAtPos() {
        World currentWorld = getWorld();
        return currentWorld == null ? net.minecraft.block.Blocks.AIR.getDefaultState()
            : currentWorld.getBlockState(getBlockPos());
    }
    /** Updates the cached supporting block position used by movement code. */
    public void updateSupportingBlockPos(boolean onGround, Vec3d movement) {
        Box box = getBoundingBox();
        World currentWorld = getWorld();
        if (currentWorld == null || !onGround) {
            java.util.Optional.empty();
            return;
        }
        java.util.Optional<BlockPos> first = currentWorld.findSupportingBlockPos(this, box);
        java.util.Optional<BlockPos> second = currentWorld.findSupportingBlockPos(this, box);
        if (first.isEmpty() && second.isEmpty()) java.util.Optional.empty();
    }
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
    public Iterable<Entity> getPassengersDeep() {
        java.util.ArrayList<Entity> result = new java.util.ArrayList<>();
        java.util.Set<Entity> visited = java.util.Collections.newSetFromMap(new java.util.IdentityHashMap<>());
        appendPassengers(this, result, visited);
        return java.util.Collections.unmodifiableList(result);
    }
    /** Vanilla stream view used by passenger traversal mixins. */
    public java.util.stream.Stream<Entity> streamIntoPassengers() {
        return java.util.stream.StreamSupport.stream(getPassengersDeep().spliterator(), false);
    }
    /** Mojang-mapped name used by Lithium's deep-passenger optimization. */
    public java.util.stream.Stream<Entity> streamSelfAndPassengers() {
        return java.util.stream.Stream.concat(java.util.stream.Stream.of(this), streamIntoPassengers());
    }
    /** Alternate 1.21.4 mapped spelling used by Lithium's allocator mixin. */
    public java.util.stream.Stream<Entity> streamPassengersAndSelf() {
        return streamSelfAndPassengers();
    }
    private static void appendPassengers(Entity root, java.util.List<Entity> result,
                                         java.util.Set<Entity> visited) {
        for (Entity passenger : root.getPassengerList()) {
            if (passenger == null || !visited.add(passenger)) continue;
            result.add(passenger);
            appendPassengers(passenger, result, visited);
        }
    }
    /** Native server entities are authoritative in the local JVM shadow. */
    public boolean isControlledByLocalInstance() { return true; }
    /** Yarn name used by 1.21.4 mixins compiled from Mojang mappings. */
    public boolean isLogicalSideForUpdatingMovement() { return isControlledByLocalInstance(); }
    /** Vanilla movement-particle predicate used by the 1.21.4 base-tick path. */
    public boolean shouldSpawnSprintingParticles() { return false; }
    /** Base entity tick hook used by FallingBlockEntity and common mixins. */
    public void tick() { age++; }
    /** Intermediary 1.21.4 base-tick entrypoint used by entity mixins. */
    public void baseTick() {
        if (hasVehicle()) {
            // Keep the vanilla vehicle check as a stable injection boundary.
        }
        if (shouldSpawnSprintingParticles()) {
            // Keep the sprint-particle call shape visible to Redirect mixins.
        }
        tick();
    }
    public void playSound(Object sound, float volume, float soundPitch) { }

    public enum RemovalReason { KILLED, DISCARDED, UNLOADED_TO_CHUNK, UNLOADED_WITH_PLAYER, CHANGED_DIMENSION }
}
