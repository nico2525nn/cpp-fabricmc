package net.fabricmc.fabric.api.event;

import java.lang.reflect.Array;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.PriorityQueue;
import java.util.Set;
import java.util.function.Function;
import net.minecraft.util.Identifier;

/** Factory for Fabric-compatible array-backed event implementations. */
public final class EventFactory {
    private EventFactory() { }

    public static <T> Event<T> createArrayBacked(Class<? super T> type,
                                                  Function<T[], T> invokerFactory) {
        return new ArrayBackedEvent<>(type, null, invokerFactory);
    }

    public static <T> Event<T> createArrayBacked(Class<T> type, T emptyInvoker,
                                                  Function<T[], T> invokerFactory) {
        return new ArrayBackedEvent<>(type, emptyInvoker, invokerFactory);
    }

    public static <T> Event<T> createWithPhases(Class<? super T> type,
                                                 Function<T[], T> invokerFactory,
                                                 Identifier... phases) {
        if (phases == null) throw new NullPointerException("phases");
        Set<String> uniquePhases = new LinkedHashSet<>();
        for (Identifier phase : phases) {
            if (phase == null)
                throw new IllegalArgumentException("event phases cannot contain null");
            if (!uniquePhases.add(phase.toString()))
                throw new IllegalArgumentException(
                    "duplicate event phase: " + phase);
        }
        if (!uniquePhases.contains(Event.DEFAULT_PHASE.toString()))
            throw new IllegalArgumentException(
                "event phases must contain " + Event.DEFAULT_PHASE);

        ArrayBackedEvent<T> event = new ArrayBackedEvent<>(type, null, invokerFactory);
        for (Identifier phase : phases) event.declarePhase(phase);
        for (int index = 1; index < phases.length; ++index)
            event.addPhaseOrdering(phases[index - 1], phases[index]);
        return event;
    }

    public static String getHandlerName(Object handler) {
        return handler == null ? "null" : handler.getClass().getName();
    }

    public static boolean isProfilingEnabled() { return false; }

    public static void invalidate() { }

    private static final class ArrayBackedEvent<T> extends Event<T> {
        private final Function<T[], T> invokerFactory;
        private final Class<?> listenerType;
        private final T emptyInvoker;
        private final Map<String, List<T>> phaseListeners = new LinkedHashMap<>();
        private final Map<String, Set<String>> phaseOrdering = new LinkedHashMap<>();
        private final List<String> declaredPhases = new ArrayList<>();

        private ArrayBackedEvent(Class<?> listenerType, T emptyInvoker,
                                 Function<T[], T> invokerFactory) {
            if (listenerType == null) throw new NullPointerException("type");
            if (invokerFactory == null) throw new NullPointerException("invokerFactory");
            this.listenerType = listenerType;
            this.emptyInvoker = emptyInvoker;
            this.invokerFactory = invokerFactory;
            this.invoker = buildInvoker();
        }

        @Override
        public void register(T listener) {
            register(DEFAULT_PHASE, listener);
        }

        @Override
        public synchronized void register(Identifier phase, T listener) {
            if (listener == null) throw new NullPointerException("listener");
            String key = phaseKey(phase);
            phaseListeners.computeIfAbsent(key, ignored -> new ArrayList<>()).add(listener);
            if (!declaredPhases.contains(key)) declaredPhases.add(key);
            rebuildInvoker();
        }

        @Override
        public synchronized void addPhaseOrdering(Identifier first, Identifier second) {
            String before = phaseKey(first);
            String after = phaseKey(second);
            if (before.equals(after))
                throw new IllegalArgumentException("event phase cannot be ordered before itself: " + before);
            Set<String> successors = phaseOrdering.computeIfAbsent(
                before, ignored -> new LinkedHashSet<>());
            if (!successors.add(after)) return;
            try {
                orderedPhases();
            } catch (IllegalArgumentException cycle) {
                successors.remove(after);
                if (successors.isEmpty()) phaseOrdering.remove(before);
                throw cycle;
            }
            rebuildInvoker();
        }

        private synchronized void declarePhase(Identifier phase) {
            String key = phaseKey(phase);
            if (!declaredPhases.contains(key)) declaredPhases.add(key);
            phaseListeners.computeIfAbsent(key, ignored -> new ArrayList<>());
            rebuildInvoker();
        }

        @Override
        public synchronized List<T> snapshot() {
            return List.copyOf(flattenListeners());
        }

        @Override
        public synchronized void clear() {
            phaseListeners.clear();
            declaredPhases.clear();
            phaseOrdering.clear();
            rebuildInvoker();
        }

        private void rebuildInvoker() { invoker = buildInvoker(); }

        private T buildInvoker() {
            List<T> listeners = flattenListeners();
            if (listeners.isEmpty() && emptyInvoker != null) return emptyInvoker;
            @SuppressWarnings("unchecked")
            T[] array = (T[]) Array.newInstance(listenerType, listeners.size());
            return invokerFactory.apply(listeners.toArray(array));
        }

        private List<T> flattenListeners() {
            if (phaseListeners.isEmpty()) return new ArrayList<>();
            List<T> result = new ArrayList<>();
            for (String phase : orderedPhases())
                result.addAll(phaseListeners.getOrDefault(phase, List.of()));
            return result;
        }

        private List<String> orderedPhases() {
            Set<String> phases = new LinkedHashSet<>(declaredPhases);
            phases.addAll(phaseListeners.keySet());
            for (Map.Entry<String, Set<String>> ordering : phaseOrdering.entrySet()) {
                phases.add(ordering.getKey());
                phases.addAll(ordering.getValue());
            }

            Map<String, Integer> declarationOrder = new LinkedHashMap<>();
            Map<String, Integer> incomingEdges = new LinkedHashMap<>();
            for (String phase : phases) {
                declarationOrder.put(phase, declarationOrder.size());
                incomingEdges.put(phase, 0);
            }
            for (Map.Entry<String, Set<String>> ordering : phaseOrdering.entrySet())
                for (String successor : ordering.getValue())
                    incomingEdges.compute(successor,
                        (ignored, count) -> count == null ? 1 : count + 1);

            PriorityQueue<String> ready = new PriorityQueue<>((left, right) -> {
                int order = Integer.compare(declarationOrder.get(left),
                                            declarationOrder.get(right));
                return order != 0 ? order : left.compareTo(right);
            });
            incomingEdges.forEach((phase, count) -> {
                if (count == 0) ready.add(phase);
            });

            List<String> ordered = new ArrayList<>(phases.size());
            while (!ready.isEmpty()) {
                String phase = ready.remove();
                ordered.add(phase);
                for (String successor : phaseOrdering.getOrDefault(phase, Set.of())) {
                    int remaining = incomingEdges.compute(successor,
                        (ignored, count) -> count - 1);
                    if (remaining == 0) ready.add(successor);
                }
            }
            if (ordered.size() != phases.size())
                throw new IllegalArgumentException("cyclic event phase ordering");
            return ordered;
        }

        private static String phaseKey(Identifier phase) {
            return (phase == null ? DEFAULT_PHASE : phase).toString();
        }
    }
}
