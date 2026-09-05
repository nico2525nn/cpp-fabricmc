package net.fabricmc.fabric.api.event;

import java.lang.reflect.Array;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Consumer;
import java.util.function.Function;

/** Small EventFactory-compatible registration object. */
public class Event<T> {
    private final Consumer<T> registrar;
    private final Function<T[], T> invokerFactory;
    private final Class<T> type;
    private final T emptyInvoker;
    private final Map<String, List<T>> phaseListeners = new LinkedHashMap<>();

    public Event(Consumer<T> registrar) { this(registrar, null, null); }
    public Event(Class<T> type, Function<T[], T> invokerFactory) {
        this(null, type, invokerFactory, null);
    }
    public Event(Consumer<T> registrar, Class<T> type, Function<T[], T> invokerFactory) {
        this(registrar, type, invokerFactory, null);
    }
    public Event(Consumer<T> registrar, Class<T> type, T emptyInvoker, Function<T[], T> invokerFactory) {
        this(registrar, type, invokerFactory, emptyInvoker);
    }
    private Event(Consumer<T> registrar, Class<T> type, Function<T[], T> invokerFactory, T emptyInvoker) {
        this.registrar = registrar;
        this.type = type;
        this.invokerFactory = invokerFactory;
        this.emptyInvoker = emptyInvoker;
    }
    public synchronized void register(T listener) {
        if (listener == null) throw new NullPointerException("listener");
        phaseListeners.computeIfAbsent("", ignored -> new ArrayList<>()).add(listener);
        if (registrar != null) registrar.accept(listener);
    }
    public synchronized void register(net.minecraft.util.Identifier phase, T listener) {
        if (listener == null) throw new NullPointerException("listener");
        String id = phase == null ? "" : phase.toString(); phaseListeners.computeIfAbsent(id, ignored -> new ArrayList<>()).add(listener);
        if (registrar != null) registrar.accept(listener);
    }
    public synchronized void addPhaseOrdering(net.minecraft.util.Identifier first, net.minecraft.util.Identifier second) { }
    @SuppressWarnings("unchecked")
    public synchronized T invoker() {
        if (invokerFactory == null) return emptyInvoker;
        List<T> listeners = new ArrayList<>(); phaseListeners.values().forEach(listeners::addAll);
        if (listeners.isEmpty() && emptyInvoker != null) return emptyInvoker;
        if (type == null) return emptyInvoker;
        T[] array = (T[]) Array.newInstance(type, listeners.size()); return invokerFactory.apply(listeners.toArray(array));
    }
    public synchronized List<T> snapshot() { List<T> result = new ArrayList<>(); phaseListeners.values().forEach(result::addAll); return List.copyOf(result); }
    public synchronized List<T> snapshot(net.minecraft.util.Identifier phase) { return List.copyOf(phaseListeners.getOrDefault(phase == null ? "" : phase.toString(), List.of())); }
    public synchronized void clear() { phaseListeners.clear(); }
}
