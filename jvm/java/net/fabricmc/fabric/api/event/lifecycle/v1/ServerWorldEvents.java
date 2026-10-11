package net.fabricmc.fabric.api.event.lifecycle.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.world.ServerWorld;

public final class ServerWorldEvents {
    private ServerWorldEvents() {}
    @FunctionalInterface public interface Load { void onWorldLoad(MinecraftServer server, ServerWorld world); }
    @FunctionalInterface public interface Unload { void onWorldUnload(MinecraftServer server, ServerWorld world); }
    public static final Event<Load> LOAD = EventFactory.createArrayBacked(
        Load.class, callbacks -> (server, world) -> {
            for (Load callback : callbacks) callback.onWorldLoad(server, world);
        });
    public static final Event<Unload> UNLOAD = EventFactory.createArrayBacked(
        Unload.class, callbacks -> (server, world) -> {
            for (Unload callback : callbacks) callback.onWorldUnload(server, world);
        });
    public static void clear() { LOAD.clear(); UNLOAD.clear(); }
}
