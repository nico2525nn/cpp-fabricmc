package net.fabricmc.fabric.api.event.lifecycle.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.server.MinecraftServer;

public final class ServerTickEvents {
    private ServerTickEvents() {}
    @FunctionalInterface public interface StartTick { void onStartTick(MinecraftServer server); }
    @FunctionalInterface public interface EndTick { void onEndTick(MinecraftServer server); }
    @FunctionalInterface public interface StartWorldTick { void onStartTick(net.minecraft.server.world.ServerWorld world); }
    @FunctionalInterface public interface EndWorldTick { void onEndTick(net.minecraft.server.world.ServerWorld world); }
    /** Source-compatible aliases retained for pre-1.21.4 cppfm mods. */
    @Deprecated @FunctionalInterface public interface Start extends StartTick { }
    @Deprecated @FunctionalInterface public interface End extends EndTick { }
    @Deprecated @FunctionalInterface public interface StartServerTick extends StartTick { }
    @Deprecated @FunctionalInterface public interface EndServerTick extends EndTick { }
    public static final Event<StartTick> START_SERVER_TICK = EventFactory.createArrayBacked(
        StartTick.class, callbacks -> server -> {
            for (StartTick callback : callbacks) callback.onStartTick(server);
        });
    public static final Event<EndTick> END_SERVER_TICK = EventFactory.createArrayBacked(
        EndTick.class, callbacks -> server -> {
            for (EndTick callback : callbacks) callback.onEndTick(server);
        });
    public static final Event<StartWorldTick> START_WORLD_TICK = EventFactory.createArrayBacked(
        StartWorldTick.class, callbacks -> world -> {
            for (StartWorldTick callback : callbacks) callback.onStartTick(world);
        });
    public static final Event<EndWorldTick> END_WORLD_TICK = EventFactory.createArrayBacked(
        EndWorldTick.class, callbacks -> world -> {
            for (EndWorldTick callback : callbacks) callback.onEndTick(world);
        });
    public static final Event<StartTick> START = START_SERVER_TICK;
    public static final Event<EndTick> END = END_SERVER_TICK;
    public static void clear() {
        START.clear(); END.clear(); START_SERVER_TICK.clear(); END_SERVER_TICK.clear();
        START_WORLD_TICK.clear(); END_WORLD_TICK.clear();
    }
}
