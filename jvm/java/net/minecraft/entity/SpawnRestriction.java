package net.minecraft.entity;

import net.minecraft.entity.mob.MobEntity;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.Heightmap;
import net.minecraft.world.WorldView;

/** Registry hook for custom entity spawn restrictions. */
public final class SpawnRestriction {
    private SpawnRestriction() { }

    @FunctionalInterface
    public interface SpawnPredicate<T extends MobEntity> {
        boolean test(EntityType<T> type, WorldView world, SpawnReason reason, BlockPos pos, net.minecraft.util.math.random.Random random);
    }

    public static <T extends MobEntity> void register(EntityType<T> type, SpawnLocation location,
            Heightmap.Type heightmap, SpawnPredicate<T> predicate) { }

    public static boolean canSpawn(EntityType<?> type, WorldView world, SpawnReason reason,
            BlockPos pos, net.minecraft.util.math.random.Random random) { return true; }
}
