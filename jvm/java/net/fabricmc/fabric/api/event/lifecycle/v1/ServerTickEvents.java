package net.fabricmc.fabric.api.event.lifecycle.v1;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.server.MinecraftServer;

public final class ServerTickEvents {
    private ServerTickEvents() {}
    @FunctionalInterface public interface Start { void onStartTick(MinecraftServer server); }
    @FunctionalInterface public interface End { void onEndTick(MinecraftServer server); }
    public static final Event<Start> START = new Event<>(CppModRuntime::registerTickStart, Start.class, callbacks -> server -> { for (Start callback : callbacks) callback.onStartTick(server); });
    public static final Event<End> END = new Event<>(CppModRuntime::registerTickEnd, End.class, callbacks -> server -> { for (End callback : callbacks) callback.onEndTick(server); });
    public static final Event<Start> START_SERVER_TICK = START;
    public static final Event<End> END_SERVER_TICK = END;
    public static void clear() { START.clear(); END.clear(); }
}
