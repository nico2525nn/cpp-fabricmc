package net.fabricmc.fabric.api.event;

import java.util.List;
import net.minecraft.util.Identifier;

/**
 * Fabric-compatible event contract used by the shadow API.
 *
 * Event implementations are created by {@link EventFactory}; callers register
 * listeners and dispatch only through {@link #invoker()}, matching Fabric's
 * public API instead of depending on the concrete array-backed implementation.
 */
public abstract class Event<T> {
    public static final Identifier DEFAULT_PHASE = Identifier.of("fabric", "default");

    protected volatile T invoker;

    public Event() { }

    public final T invoker() { return invoker; }

    public abstract void register(T listener);

    public void register(Identifier phase, T listener) {
        register(listener);
    }

    public void addPhaseOrdering(Identifier first, Identifier second) { }

    /** cppfm-only reset hook; absent from the official Fabric Event API. */
    public synchronized void clear() { }

    /** cppfm-only inspection hook; absent from the official Fabric Event API. */
    public synchronized List<T> snapshot() { return List.of(); }
}
