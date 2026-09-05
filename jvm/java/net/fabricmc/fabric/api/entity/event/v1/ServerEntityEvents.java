package net.fabricmc.fabric.api.entity.event.v1;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.entity.Entity;
import net.minecraft.server.world.ServerWorld;

/** Entity load/unload hooks backed by the native spawn boundary. */
public final class ServerEntityEvents {
    private ServerEntityEvents() { }
    @FunctionalInterface public interface Load { void onLoad(Entity entity, ServerWorld world); }
    @FunctionalInterface public interface Unload { void onUnload(Entity entity, ServerWorld world); }

    public static final Event<Load> LOAD = new Event<>(CppModRuntime::registerEntityLoad,
        Load.class, callbacks -> (entity, world) -> {
            for (Load callback : callbacks) callback.onLoad(entity, world);
        });
    public static final Event<Unload> UNLOAD = new Event<>(CppModRuntime::registerEntityUnload,
        Unload.class, callbacks -> (entity, world) -> {
            for (Unload callback : callbacks) callback.onUnload(entity, world);
        });
    public static void clear() { LOAD.clear(); UNLOAD.clear(); }
}
