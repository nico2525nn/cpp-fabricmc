package net.fabricmc.fabric.api.event.lifecycle.v1;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.world.ServerWorld;

public final class ServerWorldEvents {
    private ServerWorldEvents() {}
    @FunctionalInterface public interface Load { void onWorldLoad(MinecraftServer server, ServerWorld world); }
    @FunctionalInterface public interface Unload { void onWorldUnload(MinecraftServer server, ServerWorld world); }
    public static final Event<Load> LOAD = new Event<>(CppModRuntime::registerWorldLoad);
    public static final Event<Unload> UNLOAD = new Event<>(CppModRuntime::registerWorldUnload);
    public static void clear() { LOAD.clear(); UNLOAD.clear(); }
}
