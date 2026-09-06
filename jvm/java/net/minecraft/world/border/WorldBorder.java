package net.minecraft.world.border;

import net.minecraft.entity.Entity;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.util.math.ChunkPos;
import net.minecraft.util.math.Vec3d;
import net.minecraft.util.shape.VoxelShape;
import net.minecraft.util.shape.VoxelShapes;
import java.util.ArrayList;
import java.util.List;

/** Canonical 1.21.4 world-border view over the shadow border state. */
public class WorldBorder extends net.minecraft.world.WorldBorder {
    public static final double MAX_CENTER_COORDINATES = 2.9999984E7;
    private static final double STATIC_AREA_SIZE = 5.9999968E7;
    private double safeZone = 5.0;
    private double damagePerBlock = 0.2;
    private int warningBlocks = 5;
    private int warningTime = 15;
    private int maxRadius = (int) MAX_CENTER_COORDINATES;
    private Area area;
    private final List<WorldBorderListener> listeners = new ArrayList<>();

    public static class Properties {
        public double centerX;
        public double centerZ;
        public double size = STATIC_AREA_SIZE;
        public double targetSize = size;
        public long time;
        public double safeZone = 5.0;
        public double damagePerBlock = 0.2;
        public int warningBlocks = 5;
        public int warningTime = 15;
        public Properties() {}
        public Properties(double centerX, double centerZ, double damagePerBlock,
                          double safeZone, int warningBlocks, int warningTime,
                          long sizeLerpTime, double size, double targetSize) {
            this.centerX = centerX;
            this.centerZ = centerZ;
            this.damagePerBlock = damagePerBlock;
            this.safeZone = safeZone;
            this.warningBlocks = warningBlocks;
            this.warningTime = warningTime;
            this.time = sizeLerpTime;
            this.size = size;
            this.targetSize = targetSize;
        }
    }

    /** 1.21.4 border extent contract; mods implement this to provide custom extents. */
    public interface Area {
        default void onCenterChanged() {}
        default long getSizeLerpTime() { return 0L; }
        default double getBoundNorth() { return 0.0; }
        default Area getAreaInstance() { return this; }
        default double getBoundEast() { return 0.0; }
        default VoxelShape asVoxelShape() { return VoxelShapes.empty(); }
        default double getBoundSouth() { return 0.0; }
        default double getSize() { return 0.0; }
        default double getBoundWest() { return 0.0; }
        default WorldBorderStage getStage() { return WorldBorderStage.STATIONARY; }
        default double getSizeLerpTarget() { return getSize(); }
        default double getShrinkingSpeed() { return 0.0; }
        default void onMaxRadiusChanged() {}
        default void onSizeChanged() {}
        default boolean contains(double x, double z) {
            return x >= getBoundWest() && x <= getBoundEast()
                && z >= getBoundNorth() && z <= getBoundSouth();
        }
    }

    public static class MovingArea implements Area {
        private final WorldBorder worldBorder;
        public final double oldSize;
        public final double newSize;
        public final long timeStart;
        public final long timeEnd;
        public final long timeDuration;
        public MovingArea(WorldBorder worldBorder, double oldSize, double newSize,
                          long timeDuration) {
            this(worldBorder, oldSize, newSize, System.currentTimeMillis(), timeDuration);
        }
        public MovingArea(WorldBorder worldBorder, double oldSize, double newSize,
                          long timeStart, long timeDuration) {
            this.worldBorder = worldBorder;
            this.oldSize = oldSize; this.newSize = newSize;
            this.timeStart = timeStart;
            this.timeDuration = timeDuration;
            this.timeEnd = timeStart + timeDuration;
        }
        @Override public long getSizeLerpTime() { return timeDuration; }
        @Override public double getSize() {
            if (timeDuration <= 0L) return newSize;
            long elapsed = System.currentTimeMillis() - timeStart;
            double progress = Math.max(0.0, Math.min(1.0, (double) elapsed / timeDuration));
            return oldSize + (newSize - oldSize) * progress;
        }
        @Override public double getSizeLerpTarget() { return newSize; }
        @Override public double getShrinkingSpeed() {
            return timeDuration == 0L ? 0.0 : Math.abs(newSize - oldSize) / timeDuration;
        }
        @Override public double getBoundWest() { return worldBorder.getBoundWest(); }
        @Override public double getBoundEast() { return worldBorder.getBoundEast(); }
        @Override public double getBoundNorth() { return worldBorder.getBoundNorth(); }
        @Override public double getBoundSouth() { return worldBorder.getBoundSouth(); }
        @Override public VoxelShape asVoxelShape() {
            return VoxelShapes.cuboid(getBoundWest(), -3.0E7, getBoundNorth(),
                getBoundEast(), 3.0E7, getBoundSouth());
        }
    }

    public static class StaticArea implements Area {
        private final WorldBorder worldBorder;
        private final double size;
        public StaticArea(WorldBorder worldBorder, double size) {
            this.worldBorder = worldBorder;
            this.size = Math.max(1.0, size);
        }
        public StaticArea(WorldBorder worldBorder) { this(worldBorder, worldBorder.getSize()); }
        @Override public double getSize() { return size; }
        @Override public double getBoundWest() { return worldBorder.getBoundWest(); }
        @Override public double getBoundEast() { return worldBorder.getBoundEast(); }
        @Override public double getBoundNorth() { return worldBorder.getBoundNorth(); }
        @Override public double getBoundSouth() { return worldBorder.getBoundSouth(); }
        @Override public VoxelShape asVoxelShape() {
            return VoxelShapes.cuboid(getBoundWest(), -3.0E7, getBoundNorth(),
                getBoundEast(), 3.0E7, getBoundSouth());
        }
    }

    public WorldBorder() {
        super();
        area = new StaticArea(this, super.getSize());
    }

    public boolean contains(BlockPos pos) {
        return pos != null && contains(pos.getX() + 0.5, pos.getZ() + 0.5);
    }

    public boolean contains(Vec3d pos) {
        return pos != null && contains(pos.x, pos.z);
    }

    public boolean contains(ChunkPos pos) {
        return pos != null && contains(
            pos.getStartX(), pos.getStartZ(), pos.getEndX() + 1.0, pos.getEndZ() + 1.0);
    }

    public boolean contains(Box box) {
        return box != null && contains(box.minX, box.minZ, box.maxX, box.maxZ);
    }

    public boolean contains(double minX, double minZ, double maxX, double maxZ) {
        return contains(minX, minZ) && contains(maxX, maxZ);
    }

    public boolean contains(double x, double z) { return getBoundWest() <= x && x <= getBoundEast()
        && getBoundNorth() <= z && z <= getBoundSouth(); }

    public boolean contains(double x, double z, double margin) {
        return getBoundWest() - margin <= x && x <= getBoundEast() + margin
            && getBoundNorth() - margin <= z && z <= getBoundSouth() + margin;
    }

    public BlockPos clampFloored(BlockPos pos) {
        if (pos == null) return null;
        return clampFloored(pos.getX(), pos.getY(), pos.getZ());
    }

    public BlockPos clampFloored(double x, double y, double z) {
        double half = getSize() / 2.0;
        int clampedX = (int) Math.floor(Math.max(getCenterX() - half,
            Math.min(getCenterX() + half, x)));
        int clampedZ = (int) Math.floor(Math.max(getCenterZ() - half,
            Math.min(getCenterZ() + half, z)));
        return new BlockPos(clampedX, (int) Math.floor(y), clampedZ);
    }

    public Vec3d clamp(Vec3d pos) { return pos == null ? null : clamp(pos.x, pos.y, pos.z); }

    public Vec3d clamp(double x, double y, double z) {
        double half = getSize() / 2.0;
        return new Vec3d(
            Math.max(getCenterX() - half, Math.min(getCenterX() + half, x)),
            y,
            Math.max(getCenterZ() - half, Math.min(getCenterZ() + half, z)));
    }

    public double getDistanceInsideBorder(double x, double z) {
        double half = getSize() / 2.0;
        return Math.min(half - Math.abs(x - getCenterX()), half - Math.abs(z - getCenterZ()));
    }

    public double getDistanceInsideBorder(Entity entity) {
        return entity == null ? 0.0 : getDistanceInsideBorder(entity.getX(), entity.getZ());
    }

    @Override public void setCenter(double x, double z) {
        super.setCenter(x, z);
        if (area != null) area.onCenterChanged();
        for (WorldBorderListener listener : listenerSnapshot()) listener.onCenterChanged(this, x, z);
    }

    @Override public double getSize() { return area == null ? super.getSize() : area.getSize(); }

    @Override public void setSize(double value) {
        double size = Math.max(1.0, value);
        super.setSize(size);
        area = new StaticArea(this, size);
        for (WorldBorderListener listener : listenerSnapshot()) listener.onSizeChange(this, size);
    }

    public void interpolateSize(double fromSize, double toSize, long time) {
        super.setSize(Math.max(1.0, fromSize));
        area = new MovingArea(this, Math.max(1.0, fromSize), Math.max(1.0, toSize), time);
        for (WorldBorderListener listener : listenerSnapshot())
            listener.onInterpolateSize(this, fromSize, toSize, time);
    }
    public void lerpSizeBetween(double fromSize, double toSize, long time) {
        interpolateSize(fromSize, toSize, time);
    }

    public long getSizeLerpTime() { return area == null ? 0L : area.getSizeLerpTime(); }
    public double getSizeLerpTarget() { return area == null ? getSize() : area.getSizeLerpTarget(); }
    public double getShrinkingSpeed() { return area == null ? 0.0 : area.getShrinkingSpeed(); }
    public void tick() {
        if (area instanceof MovingArea moving && System.currentTimeMillis() >= moving.timeEnd) setSize(moving.newSize);
    }

    public double getSafeZone() { return safeZone; }
    public void setSafeZone(double value) {
        safeZone = Math.max(0.0, value);
        for (WorldBorderListener listener : listenerSnapshot()) listener.onSafeZoneChanged(this, safeZone);
    }
    public double getDamagePerBlock() { return damagePerBlock; }
    public void setDamagePerBlock(double value) {
        damagePerBlock = value;
        for (WorldBorderListener listener : listenerSnapshot()) listener.onDamagePerBlockChanged(this, value);
    }
    public int getWarningBlocks() { return warningBlocks; }
    public void setWarningBlocks(int value) {
        warningBlocks = value;
        for (WorldBorderListener listener : listenerSnapshot()) listener.onWarningBlocksChanged(this, value);
    }
    public int getWarningTime() { return warningTime; }
    public void setWarningTime(int value) {
        warningTime = value;
        for (WorldBorderListener listener : listenerSnapshot()) listener.onWarningTimeChanged(this, value);
    }
    public int getMaxRadius() { return maxRadius; }
    public void setMaxRadius(int value) {
        maxRadius = Math.max(1, value);
        if (area != null) area.onMaxRadiusChanged();
    }
    public void addListener(WorldBorderListener listener) {
        if (listener != null && !listeners.contains(listener)) listeners.add(listener);
    }
    public void removeListener(WorldBorderListener listener) { listeners.remove(listener); }
    public List<WorldBorderListener> getListeners() { return List.copyOf(listenerSnapshot()); }
    public Area getAreaInstance() { return area == null ? new StaticArea(this, super.getSize()) : area.getAreaInstance(); }
    public double getBoundWest() { return getCenterX() - getSize() / 2.0; }
    public double getBoundEast() { return getCenterX() + getSize() / 2.0; }
    public double getBoundNorth() { return getCenterZ() - getSize() / 2.0; }
    public double getBoundSouth() { return getCenterZ() + getSize() / 2.0; }
    public VoxelShape asVoxelShape() {
        return VoxelShapes.cuboid(getBoundWest(), -3.0E7, getBoundNorth(),
            getBoundEast(), 3.0E7, getBoundSouth());
    }

    public WorldBorderStage getStage() { return WorldBorderStage.STATIONARY; }

    private List<WorldBorderListener> listenerSnapshot() { return new ArrayList<>(listeners); }
}
