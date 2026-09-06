package net.minecraft.entity.ai.pathing;

import net.minecraft.entity.Entity;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;

/** Minimal navigation handle used by the MobEntity ABI. */
public class EntityNavigation {
    protected final MobEntity entity;
    protected final World world;
    public EntityNavigation(MobEntity entity, World world) { this.entity = entity; this.world = world; }
    public boolean startMovingTo(double x, double y, double z, double speed) { return false; }
    public boolean startMovingTo(Entity target, double speed) { return false; }
    public boolean startMovingTo(BlockPos pos, double speed) { return false; }
    public boolean startMovingAlong(Path path, double speed) { return path != null; }
    public Path findPathTo(BlockPos pos, int distance) {
        return pos == null ? null : new Path(pos, false);
    }
    public Path findPathTo(Entity target, int distance) {
        return target == null ? null : new Path(target.getBlockPos(), false);
    }
    public void stop() { }
    public void recalculatePath() { }
    public boolean isIdle() { return true; }
    public boolean isFollowingPath() { return false; }
}
