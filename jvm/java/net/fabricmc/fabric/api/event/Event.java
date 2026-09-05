package net.fabricmc.fabric.api.event;

import java.lang.reflect.Array;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;
import java.util.function.Function;

/** Small EventFactory-compatible registration object. */
public class Event<T> {
    private final Consumer<T> registrar;
    private final Function<T[], T> invokerFactory;
    private final Class<T> type;
    private final List<T> listeners = new ArrayList<>();

    public Event(Consumer<T> registrar) { this(registrar, null, null); }
    public Event(Class<T> type, Function<T[], T> invokerFactory) {
        this(null, type, invokerFactory);
    }
    private Event(Consumer<T> registrar, Class<T> type, Function<T[], T> invokerFactory) {
        this.registrar = registrar;
        this.type = type;
        this.invokerFactory = invokerFactory;
    }
    public synchronized void register(T listener) {
        if (listener == null) throw new NullPointerException("listener");
        listeners.add(listener);
        if (registrar != null) registrar.accept(listener);
    }
    @SuppressWarnings("unchecked")
    public synchronized T invoker() {
        if (invokerFactory == null) return null;
        T[] array = (T[]) Array.newInstance(type, listeners.size());
        return invokerFactory.apply(listeners.toArray(array));
    }
    public synchronized List<T> snapshot() { return List.copyOf(listeners); }
    public synchronized void clear() { listeners.clear(); }
}
