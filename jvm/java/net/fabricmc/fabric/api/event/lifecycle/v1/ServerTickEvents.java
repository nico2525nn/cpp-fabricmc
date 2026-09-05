package net.fabricmc.fabric.api.event.lifecycle.v1;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.server.MinecraftServer;

public final class ServerTickEvents {
    private ServerTickEvents() {}
    @FunctionalInterface public interface Start { void onStartTick(MinecraftServer server); }
    @FunctionalInterface public interface End { void onEndTick(MinecraftServer server); }
    @FunctionalInterface public interface StartServerTick { void onStartTick(MinecraftServer server); }
    @FunctionalInterface public interface EndServerTick { void onEndTick(MinecraftServer server); }
    @FunctionalInterface public interface StartWorldTick { void onStartTick(net.minecraft.server.world.ServerWorld world); }
    @FunctionalInterface public interface EndWorldTick { void onEndTick(net.minecraft.server.world.ServerWorld world); }
    /** Compatibility aliases used by a few older Fabric API snapshots. */
    @FunctionalInterface public interface StartTick extends StartServerTick { }
    @FunctionalInterface public interface EndTick extends EndServerTick { }
    public static final Event<Start> START = new Event<>(CppModRuntime::registerTickStart, Start.class, callbacks -> server -> { for (Start callback : callbacks) callback.onStartTick(server); });
    public static final Event<End> END = new Event<>(CppModRuntime::registerTickEnd, End.class, callbacks -> server -> { for (End callback : callbacks) callback.onEndTick(server); });
    public static final Event<StartServerTick> START_SERVER_TICK = new Event<>(CppModRuntime::registerStartServerTick, StartServerTick.class, callbacks -> server -> { for (StartServerTick callback : callbacks) callback.onStartTick(server); });
    public static final Event<EndServerTick> END_SERVER_TICK = new Event<>(CppModRuntime::registerEndServerTick, EndServerTick.class, callbacks -> server -> { for (EndServerTick callback : callbacks) callback.onEndTick(server); });
    public static final Event<StartWorldTick> START_WORLD_TICK = new Event<>(CppModRuntime::registerStartWorldTick, StartWorldTick.class, callbacks -> world -> { for (StartWorldTick callback : callbacks) callback.onStartTick(world); });
    public static final Event<EndWorldTick> END_WORLD_TICK = new Event<>(CppModRuntime::registerEndWorldTick, EndWorldTick.class, callbacks -> world -> { for (EndWorldTick callback : callbacks) callback.onEndTick(world); });
    public static void clear() {
        START.clear(); END.clear(); START_SERVER_TICK.clear(); END_SERVER_TICK.clear();
        START_WORLD_TICK.clear(); END_WORLD_TICK.clear();
    }
}
