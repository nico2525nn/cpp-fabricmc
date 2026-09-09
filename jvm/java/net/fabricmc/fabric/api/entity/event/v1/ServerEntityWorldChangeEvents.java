package net.fabricmc.fabric.api.entity.event.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.entity.Entity;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;

public final class ServerEntityWorldChangeEvents {
    private ServerEntityWorldChangeEvents() { }
    @FunctionalInterface public interface AfterEntityChange {
        void afterChangeWorld(Entity entity, Entity origin, ServerWorld destination, ServerWorld originWorld);
    }
    @FunctionalInterface public interface AfterPlayerChange {
        void afterChangeWorld(ServerPlayerEntity player, ServerWorld origin, ServerWorld destination);
    }
    public static final Event<AfterEntityChange> AFTER_ENTITY_CHANGE_WORLD = EventFactory.createArrayBacked(
        AfterEntityChange.class, callbacks -> (entity, origin, destination, originWorld) -> {
            for (AfterEntityChange callback : callbacks)
                callback.afterChangeWorld(entity, origin, destination, originWorld);
        });
    public static final Event<AfterPlayerChange> AFTER_PLAYER_CHANGE_WORLD = EventFactory.createArrayBacked(
        AfterPlayerChange.class, callbacks -> (player, origin, destination) -> {
            for (AfterPlayerChange callback : callbacks)
                callback.afterChangeWorld(player, origin, destination);
        });
    public static void clear() { AFTER_ENTITY_CHANGE_WORLD.clear(); AFTER_PLAYER_CHANGE_WORLD.clear(); }
}
