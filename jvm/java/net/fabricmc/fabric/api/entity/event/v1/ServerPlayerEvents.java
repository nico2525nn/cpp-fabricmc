package net.fabricmc.fabric.api.entity.event.v1;

import cppfm.bridge.CppModRuntime;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import net.minecraft.server.network.ServerPlayerEntity;

/** Server-thread player lifecycle callbacks exposed by Fabric API. */
public final class ServerPlayerEvents {
    private ServerPlayerEvents() { }

    @FunctionalInterface
    public interface CopyFrom {
        void copyFrom(ServerPlayerEntity oldPlayer, ServerPlayerEntity newPlayer, boolean alive);
    }
    @FunctionalInterface
    public interface AfterRespawn {
        void afterRespawn(ServerPlayerEntity oldPlayer, ServerPlayerEntity newPlayer, boolean alive);
    }
    @FunctionalInterface
    public interface Join {
        void onPlayReady(ServerPlayNetworkHandler handler, ServerPlayerEntity player);
    }
    @FunctionalInterface
    public interface Leave {
        void onPlayDisconnect(ServerPlayNetworkHandler handler, ServerPlayerEntity player);
    }

    public static final Event<CopyFrom> COPY_FROM = new Event<>(CppModRuntime::registerPlayerCopyFrom,
        CopyFrom.class, callbacks -> (oldPlayer, newPlayer, alive) -> {
            for (CopyFrom callback : callbacks) callback.copyFrom(oldPlayer, newPlayer, alive);
        });
    public static final Event<AfterRespawn> AFTER_RESPAWN = new Event<>(CppModRuntime::registerPlayerAfterRespawn,
        AfterRespawn.class, callbacks -> (oldPlayer, newPlayer, alive) -> {
            for (AfterRespawn callback : callbacks) callback.afterRespawn(oldPlayer, newPlayer, alive);
        });
    public static final Event<Join> JOIN = new Event<>(CppModRuntime::registerPlayerJoinEvent,
        Join.class, callbacks -> (handler, player) -> {
            for (Join callback : callbacks) callback.onPlayReady(handler, player);
        });
    public static final Event<Leave> LEAVE = new Event<>(CppModRuntime::registerPlayerLeaveEvent,
        Leave.class, callbacks -> (handler, player) -> {
            for (Leave callback : callbacks) callback.onPlayDisconnect(handler, player);
        });

    public static void clear() {
        COPY_FROM.clear(); AFTER_RESPAWN.clear(); JOIN.clear(); LEAVE.clear();
    }
}
