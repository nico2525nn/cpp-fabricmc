package com.mojang.brigadier.context;

import java.util.HashMap;
import java.util.Map;

public final class CommandContext<S> {
    private final S source;
    private final String input;
    private final Map<String, Object> arguments;
    public CommandContext(S source, String input) {
        this(source, input, new HashMap<>());
    }
    public CommandContext(S source, String input, Map<String, Object> arguments) {
        this.source = source;
        this.input = input == null ? "" : input;
        this.arguments = arguments == null ? new HashMap<>() : arguments;
    }
    public S getSource() { return source; }
    public String getInput() { return input; }
    public <V> V getArgument(String name, Class<V> type) {
        Object value = arguments.get(name);
        return value == null ? null : type.cast(value);
    }
    public <V> V getArgumentOrDefault(String name, Class<V> type, V fallback) {
        V value = getArgument(name, type);
        return value == null ? fallback : value;
    }
    public void putArgument(String name, Object value) { arguments.put(name, value); }
}
