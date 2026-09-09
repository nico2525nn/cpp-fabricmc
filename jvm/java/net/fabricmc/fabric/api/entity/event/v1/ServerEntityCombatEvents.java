package net.fabricmc.fabric.api.entity.event.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.server.world.ServerWorld;

public final class ServerEntityCombatEvents {
    private ServerEntityCombatEvents() { }
    @FunctionalInterface public interface AfterKilledOtherEntity {
        void afterKilledOtherEntity(ServerWorld world, Entity entity, LivingEntity killedEntity);
    }
    public static final Event<AfterKilledOtherEntity> AFTER_KILLED_OTHER_ENTITY =
        EventFactory.createArrayBacked(AfterKilledOtherEntity.class, callbacks -> (world, entity, killed) -> {
            for (AfterKilledOtherEntity callback : callbacks)
                callback.afterKilledOtherEntity(world, entity, killed);
        });
    public static void clear() { AFTER_KILLED_OTHER_ENTITY.clear(); }
}
