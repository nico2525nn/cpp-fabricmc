package net.fabricmc.fabric.api.event.lifecycle.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.block.entity.BlockEntity;
import net.minecraft.server.world.ServerWorld;

public final class ServerBlockEntityEvents {
    private ServerBlockEntityEvents() { }
    @FunctionalInterface public interface Load { void onLoad(BlockEntity blockEntity, ServerWorld world); }
    @FunctionalInterface public interface Unload { void onUnload(BlockEntity blockEntity, ServerWorld world); }
    public static final Event<Load> BLOCK_ENTITY_LOAD = EventFactory.createArrayBacked(
        Load.class, callbacks -> (entity, world) -> {
            for (Load callback : callbacks) callback.onLoad(entity, world);
        });
    public static final Event<Unload> BLOCK_ENTITY_UNLOAD = EventFactory.createArrayBacked(
        Unload.class, callbacks -> (entity, world) -> {
            for (Unload callback : callbacks) callback.onUnload(entity, world);
        });
    public static void clear() { BLOCK_ENTITY_LOAD.clear(); BLOCK_ENTITY_UNLOAD.clear(); }
}
