package net.minecraft.entity.ai.brain.task;

import net.minecraft.entity.LivingEntity;
import net.minecraft.server.world.ServerWorld;
import java.util.Map;

/** Minimal state carrier used by the 1.21.4 Task ABI and mixin shadows. */
public abstract class MultiTickTask<E extends LivingEntity> implements Task<E> {
    public static final int DEFAULT_RUN_TIME = 60;
    protected final Map<?, ?> requiredMemoryStates;
    protected Status status = Status.STOPPED;
    protected long endTime;
    protected final int minRunTime;
    protected final int maxRunTime;

    protected MultiTickTask() {
        this(Map.of(), DEFAULT_RUN_TIME, DEFAULT_RUN_TIME);
    }

    protected MultiTickTask(Map<?, ?> requiredMemoryStates) {
        this(requiredMemoryStates, DEFAULT_RUN_TIME, DEFAULT_RUN_TIME);
    }

    protected MultiTickTask(Map<?, ?> requiredMemoryStates, int minRunTime, int maxRunTime) {
        this.requiredMemoryStates = requiredMemoryStates == null ? Map.of() : requiredMemoryStates;
        this.minRunTime = minRunTime;
        this.maxRunTime = maxRunTime;
    }

    @Override public String getName() { return getClass().getSimpleName(); }
    @Override public Status getStatus() { return status; }
    @Override public void stop(ServerWorld world, E entity, long time) { status = Status.STOPPED; }
    @Override public void tick(ServerWorld world, E entity, long time) {}
    @Override public boolean tryStarting(ServerWorld world, E entity, long time) {
        status = Status.RUNNING;
        endTime = time + maxRunTime;
        return true;
    }

    public enum Status { STOPPED, RUNNING }
}
