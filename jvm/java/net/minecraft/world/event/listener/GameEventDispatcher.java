package net.minecraft.world.event.listener;

import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.event.GameEvent;

/** Minimal server-side game-event dispatcher ABI for 1.21.4 chunk mixins. */
public interface GameEventDispatcher {
    GameEventDispatcher EMPTY = new GameEventDispatcher() { };

    default boolean isEmpty() { return true; }

    default boolean dispatch(RegistryEntry<GameEvent> event, Vec3d pos,
                             GameEvent.Emitter emitter, DispatchCallback callback) {
        return false;
    }

    default void addListener(GameEventListener listener) { }
    default void removeListener(GameEventListener listener) { }

    @FunctionalInterface
    interface DispatchCallback {
        void visit(GameEventListener listener, Vec3d listenerPos);
    }
}
