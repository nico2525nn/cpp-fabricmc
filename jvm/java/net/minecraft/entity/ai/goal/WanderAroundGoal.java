package net.minecraft.entity.ai.goal;

import net.minecraft.entity.mob.PathAwareEntity;
import net.minecraft.util.math.Vec3d;

/** Wander goal ABI referenced by Carpet helper code. */
public class WanderAroundGoal extends Goal {
    protected final PathAwareEntity mob;
    protected double speed;
    protected int chance;
    protected boolean canDespawn;

    public WanderAroundGoal(PathAwareEntity mob, double speed) { this(mob, speed, 120); }
    public WanderAroundGoal(PathAwareEntity mob, double speed, int chance) {
        this(mob, speed, chance, true);
    }
    public WanderAroundGoal(PathAwareEntity mob, double speed, int chance, boolean canDespawn) {
        this.mob = mob; this.speed = speed; this.chance = chance; this.canDespawn = canDespawn;
    }
    public void setChance(int value) { chance = value; }
    public void ignoreChanceOnce() { chance = 0; }
    public Vec3d getWanderTarget() { return null; }
}
