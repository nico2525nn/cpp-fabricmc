package net.fabricmc.fabric.api.networking.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.entity.Entity;
import net.minecraft.server.network.ServerPlayerEntity;

public final class EntityTrackingEvents {
    private EntityTrackingEvents() { }
    @FunctionalInterface public interface StartTracking {
        void onStartTracking(Entity entity, ServerPlayerEntity player);
    }
    @FunctionalInterface public interface StopTracking {
        void onStopTracking(Entity entity, ServerPlayerEntity player);
    }
    public static final Event<StartTracking> START_TRACKING = EventFactory.createArrayBacked(
        StartTracking.class, callbacks -> (entity, player) -> {
            for (StartTracking callback : callbacks) callback.onStartTracking(entity, player);
        });
    public static final Event<StopTracking> STOP_TRACKING = EventFactory.createArrayBacked(
        StopTracking.class, callbacks -> (entity, player) -> {
            for (StopTracking callback : callbacks) callback.onStopTracking(entity, player);
        });
    public static void clear() { START_TRACKING.clear(); STOP_TRACKING.clear(); }
}
