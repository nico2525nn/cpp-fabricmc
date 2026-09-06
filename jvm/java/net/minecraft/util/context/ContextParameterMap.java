package net.minecraft.util.context;

import java.util.HashMap;
import java.util.Map;
import java.util.Objects;

/** Immutable values supplied to a context-aware Minecraft operation. */
public final class ContextParameterMap {
    private final Map<ContextParameter<?>, Object> map;

    public ContextParameterMap(Map<ContextParameter<?>, Object> map) {
        this.map = Map.copyOf(map == null ? Map.of() : map);
    }

    public boolean contains(ContextParameter<?> parameter) {
        return map.containsKey(parameter);
    }

    @SuppressWarnings("unchecked")
    public <T> T getOrDefault(ContextParameter<T> parameter, T defaultValue) {
        return (T) map.getOrDefault(parameter, defaultValue);
    }

    @SuppressWarnings("unchecked")
    public <T> T getOrThrow(ContextParameter<T> parameter) {
        return (T) Objects.requireNonNull(map.get(parameter),
                () -> "Missing context parameter: " + parameter);
    }

    @SuppressWarnings("unchecked")
    public <T> T getNullable(ContextParameter<T> parameter) {
        return (T) map.get(parameter);
    }

    public static final class Builder {
        private final Map<ContextParameter<?>, Object> map = new HashMap<>();

        public <T> Builder add(ContextParameter<T> parameter, T value) {
            map.put(Objects.requireNonNull(parameter, "parameter"),
                    Objects.requireNonNull(value, "value"));
            return this;
        }

        public <T> Builder addNullable(ContextParameter<T> parameter, T value) {
            map.put(Objects.requireNonNull(parameter, "parameter"), value);
            return this;
        }

        public <T> T getOrThrow(ContextParameter<T> parameter) {
            @SuppressWarnings("unchecked") T value = (T) map.get(parameter);
            return Objects.requireNonNull(value, () -> "Missing context parameter: " + parameter);
        }

        @SuppressWarnings("unchecked")
        public <T> T getNullable(ContextParameter<T> parameter) {
            return (T) map.get(parameter);
        }

        public ContextParameterMap build(ContextType type) {
            if (type != null) {
                for (ContextParameter<?> parameter : type.getRequired()) {
                    if (!map.containsKey(parameter) || map.get(parameter) == null)
                        throw new IllegalArgumentException("Missing required context parameter: " + parameter);
                }
                for (ContextParameter<?> parameter : map.keySet()) {
                    if (!type.getAllowed().contains(parameter))
                        throw new IllegalArgumentException("Parameter not allowed by context type: " + parameter);
                }
            }
            return new ContextParameterMap(map);
        }
    }
}
