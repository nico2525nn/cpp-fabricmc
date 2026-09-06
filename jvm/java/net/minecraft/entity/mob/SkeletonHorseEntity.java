package net.minecraft.entity.mob;

import net.minecraft.entity.EntityType;
import net.minecraft.entity.attribute.DefaultAttributeContainer;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.random.Random;
import net.minecraft.world.ServerWorldAccess;
import net.minecraft.world.World;
import net.minecraft.world.WorldAccess;
import net.minecraft.entity.SpawnReason;

/** Skeleton-horse ABI referenced by Carpet's natural-spawn mixins. */
public class SkeletonHorseEntity extends MobEntity {
    protected int trapTime;
    protected boolean trapped;

    public SkeletonHorseEntity(EntityType<?> type, World world) { super(type, world); }
    protected SkeletonHorseEntity(long nativeHandle, World world, EntityType<?> type) { super(nativeHandle, world, type); }
    protected SkeletonHorseEntity() { super(); }

    public boolean isTrapped() { return trapped; }
    public void setTrapped(boolean value) { trapped = value; }
    public static DefaultAttributeContainer.Builder createSkeletonHorseAttributes() {
        return DefaultAttributeContainer.builder();
    }
    public static boolean canSpawn(EntityType<?> type, WorldAccess world, SpawnReason reason,
                                   BlockPos pos, Random random) { return true; }
    public static boolean canSpawn(EntityType<?> type, ServerWorldAccess world, SpawnReason reason,
                                   BlockPos pos, Random random) { return true; }
}
