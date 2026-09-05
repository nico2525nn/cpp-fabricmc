package net.fabricmc.fabric.api.event.lifecycle.v1;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.world.ServerWorld;

public final class ServerWorldEvents {
    private ServerWorldEvents() {}
    @FunctionalInterface public interface Load { void onWorldLoad(MinecraftServer server, ServerWorld world); }
    @FunctionalInterface public interface Unload { void onWorldUnload(MinecraftServer server, ServerWorld world); }
    public static final Event<Load> LOAD = new Event<>(CppModRuntime::registerWorldLoad, Load.class, callbacks -> (server, world) -> { for (Load callback : callbacks) callback.onWorldLoad(server, world); });
    public static final Event<Unload> UNLOAD = new Event<>(CppModRuntime::registerWorldUnload, Unload.class, callbacks -> (server, world) -> { for (Unload callback : callbacks) callback.onWorldUnload(server, world); });
    public static void clear() { LOAD.clear(); UNLOAD.clear(); }
}
