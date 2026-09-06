package net.minecraft.entity.ai.brain.task;

import net.minecraft.entity.LivingEntity;
import net.minecraft.server.world.ServerWorld;

/**
 * 1.21.4's task contract.  The game uses an interface here; keeping it as an
 * interface is important for mixins which implement Task directly.
 */
public interface Task<E extends LivingEntity> {
    String getName();
    MultiTickTask.Status getStatus();
    void stop(ServerWorld world, E entity, long time);
    void tick(ServerWorld world, E entity, long time);
    boolean tryStarting(ServerWorld world, E entity, long time);
}
