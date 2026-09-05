package net.fabricmc.fabric.api.event.lifecycle.v1;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.server.MinecraftServer;

public final class ServerLifecycleEvents {
    private ServerLifecycleEvents() {}
    @FunctionalInterface public interface ServerStarting { void onServerStarting(MinecraftServer server); }
    @FunctionalInterface public interface ServerStarted { void onServerStarted(MinecraftServer server); }
    @FunctionalInterface public interface ServerStopping { void onServerStopping(MinecraftServer server); }
    @FunctionalInterface public interface ServerStopped { void onServerStopped(MinecraftServer server); }

    public static final Event<ServerStarting> SERVER_STARTING = new Event<>(CppModRuntime::registerServerStarting);
    public static final Event<ServerStarted> SERVER_STARTED = new Event<>(CppModRuntime::registerServerStarted);
    public static final Event<ServerStopping> SERVER_STOPPING = new Event<>(CppModRuntime::registerServerStopping);
    public static final Event<ServerStopped> SERVER_STOPPED = new Event<>(CppModRuntime::registerServerStopped);
    public static void clear() {
        SERVER_STARTING.clear(); SERVER_STARTED.clear();
        SERVER_STOPPING.clear(); SERVER_STOPPED.clear();
    }
}
