package net.fabricmc.fabric.api.event;

import java.lang.reflect.Array;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.Consumer;
import java.util.function.Function;

/** Small EventFactory-compatible registration object. */
public class Event<T> {
    private final Consumer<T> registrar;
    private final Function<T[], T> invokerFactory;
    private final Class<T> type;
    private final T emptyInvoker;
    private final Map<String, List<T>> phaseListeners = new LinkedHashMap<>();
    private final Map<String, Set<String>> phaseOrdering = new HashMap<>();
    private final List<String> declaredPhases = new ArrayList<>();
    private volatile T cachedInvoker;

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
        this.cachedInvoker = buildInvoker();
    }
    public void register(T listener) {
        if (listener == null) throw new NullPointerException("listener");
        registerInternal("", listener);
    }
    public void register(net.minecraft.util.Identifier phase, T listener) {
        if (listener == null) throw new NullPointerException("listener");
        registerInternal(phase == null ? "" : phase.toString(), listener);
    }
    public synchronized void addPhaseOrdering(net.minecraft.util.Identifier first, net.minecraft.util.Identifier second) {
        String before = first == null ? "" : first.toString();
        String after = second == null ? "" : second.toString();
        phaseOrdering.computeIfAbsent(before, ignored -> new HashSet<>()).add(after);
        rebuildInvoker();
    }
    synchronized void declarePhase(net.minecraft.util.Identifier phase) {
        String id = phase == null ? "" : phase.toString();
        if (!declaredPhases.contains(id)) declaredPhases.add(id);
        phaseListeners.computeIfAbsent(id, ignored -> new ArrayList<>());
        rebuildInvoker();
    }
    public T invoker() { return cachedInvoker; }
    public synchronized List<T> snapshot() { return List.copyOf(flattenListeners()); }
    public synchronized List<T> snapshot(net.minecraft.util.Identifier phase) { return List.copyOf(phaseListeners.getOrDefault(phase == null ? "" : phase.toString(), List.of())); }
    public synchronized void clear() { phaseListeners.clear(); rebuildInvoker(); }

    private void registerInternal(String phase, T listener) {
        synchronized (this) {
            phaseListeners.computeIfAbsent(phase, ignored -> new ArrayList<>()).add(listener);
            if (!declaredPhases.contains(phase)) declaredPhases.add(phase);
            rebuildInvoker();
        }
        try {
            if (registrar != null) registrar.accept(listener);
        } catch (RuntimeException | Error failure) {
            synchronized (this) {
                List<T> listeners = phaseListeners.get(phase);
                if (listeners != null) {
                    listeners.remove(listener);
                    if (listeners.isEmpty()) phaseListeners.remove(phase);
                }
                rebuildInvoker();
            }
            throw failure;
        }
    }

    private synchronized void rebuildInvoker() { cachedInvoker = buildInvoker(); }

    @SuppressWarnings("unchecked")
    private T buildInvoker() {
        if (invokerFactory == null || type == null) return emptyInvoker;
        List<T> listeners = flattenListeners();
        if (listeners.isEmpty() && emptyInvoker != null) return emptyInvoker;
        T[] array = (T[]) Array.newInstance(type, listeners.size());
        return invokerFactory.apply(listeners.toArray(array));
    }

    private List<T> flattenListeners() {
        if (phaseListeners.isEmpty()) return new ArrayList<>();
        List<String> phases = new ArrayList<>();
        for (String phase : declaredPhases) if (phaseListeners.containsKey(phase)) phases.add(phase);
        for (String phase : phaseListeners.keySet()) if (!phases.contains(phase)) phases.add(phase);
        phases.sort(this::comparePhases);
        List<T> result = new ArrayList<>();
        for (String phase : phases) result.addAll(phaseListeners.getOrDefault(phase, List.of()));
        return result;
    }

    private int comparePhases(String left, String right) {
        if (left.equals(right)) return 0;
        if (reachable(left, right, new HashSet<>())) return -1;
        if (reachable(right, left, new HashSet<>())) return 1;
        int leftIndex = declaredPhases.indexOf(left);
        int rightIndex = declaredPhases.indexOf(right);
        if (leftIndex < 0) leftIndex = Integer.MAX_VALUE;
        if (rightIndex < 0) rightIndex = Integer.MAX_VALUE;
        int order = Integer.compare(leftIndex, rightIndex);
        return order != 0 ? order : left.compareTo(right);
    }

    private boolean reachable(String start, String target, Set<String> visited) {
        if (!visited.add(start)) return false;
        for (String next : phaseOrdering.getOrDefault(start, Set.of())) {
            if (next.equals(target) || reachable(next, target, visited)) return true;
        }
        return false;
    }
}
