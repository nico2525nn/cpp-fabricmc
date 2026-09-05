package net.fabricmc.fabric.api.event.lifecycle.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.entity.Entity;
import net.minecraft.server.world.ServerWorld;

public final class ServerEntityEvents {
    private ServerEntityEvents() {}
    @FunctionalInterface public interface Load { void onLoad(Entity entity, ServerWorld world); }
    @FunctionalInterface public interface Unload { void onUnload(Entity entity, ServerWorld world); }
    public static final Event<Load> LOAD = EventFactory.createArrayBacked(Load.class, callbacks -> (entity, world) -> { for (Load callback : callbacks) callback.onLoad(entity, world); });
    public static final Event<Unload> UNLOAD = EventFactory.createArrayBacked(Unload.class, callbacks -> (entity, world) -> { for (Unload callback : callbacks) callback.onUnload(entity, world); });
    public static void clear() { LOAD.clear(); UNLOAD.clear(); }
}
