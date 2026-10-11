package net.fabricmc.fabric.api.entity.event.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import net.minecraft.server.network.ServerPlayerEntity;

/** Server-thread player lifecycle callbacks exposed by Fabric API. */
public final class ServerPlayerEvents {
    private ServerPlayerEvents() { }

    @FunctionalInterface
    public interface CopyFrom {
        /** Official Fabric 1.21.4 callback name. */
        void copyFromPlayer(ServerPlayerEntity oldPlayer, ServerPlayerEntity newPlayer, boolean alive);

        /** Compatibility alias used by the native bridge. */
        default void copyFrom(ServerPlayerEntity oldPlayer, ServerPlayerEntity newPlayer, boolean alive) {
            copyFromPlayer(oldPlayer, newPlayer, alive);
        }
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

    public static final Event<CopyFrom> COPY_FROM = EventFactory.createArrayBacked(
        CopyFrom.class, callbacks -> (oldPlayer, newPlayer, alive) -> {
            for (CopyFrom callback : callbacks) callback.copyFromPlayer(oldPlayer, newPlayer, alive);
        });
    public static final Event<AfterRespawn> AFTER_RESPAWN = EventFactory.createArrayBacked(
        AfterRespawn.class, callbacks -> (oldPlayer, newPlayer, alive) -> {
            for (AfterRespawn callback : callbacks) callback.afterRespawn(oldPlayer, newPlayer, alive);
        });
    public static final Event<Join> JOIN = EventFactory.createArrayBacked(
        Join.class, callbacks -> (handler, player) -> {
            for (Join callback : callbacks) callback.onPlayReady(handler, player);
        });
    public static final Event<Leave> LEAVE = EventFactory.createArrayBacked(
        Leave.class, callbacks -> (handler, player) -> {
            for (Leave callback : callbacks) callback.onPlayDisconnect(handler, player);
        });
    /** Deprecated player-specific fatal-damage gate retained for 1.21.4 mods. */
    @Deprecated
    @FunctionalInterface
    public interface AllowDeath {
        boolean allowDeath(ServerPlayerEntity player, DamageSource damageSource, float damageAmount);
    }
    @Deprecated
    public static final Event<AllowDeath> ALLOW_DEATH = EventFactory.createArrayBacked(
        AllowDeath.class, callbacks -> (player, source, amount) -> {
            for (AllowDeath callback : callbacks)
                if (!callback.allowDeath(player, source, amount)) return false;
            return true;
        });

    public static void clear() {
        COPY_FROM.clear(); AFTER_RESPAWN.clear(); JOIN.clear(); LEAVE.clear(); ALLOW_DEATH.clear();
    }
}
