package com.mojang.brigadier.context;

import com.mojang.brigadier.Command;
import com.mojang.brigadier.tree.CommandNode;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class CommandContext<S> {
    private final S source;
    private final String input;
    private final Map<String, Object> arguments;
    private final List<CommandNode<S>> nodes;
    private final CommandNode<S> rootNode;
    private final Command<S> command;
    private final CommandContext<S> child;
    private final CommandNode<S> redirect;
    private final StringRange range;

    public CommandContext(S source, String input) { this(source, input, new HashMap<>()); }
    public CommandContext(S source, String input, Map<String, Object> arguments) {
        this(source, input, arguments, List.of(), null, null, null, null, StringRange.at(0));
    }
    public CommandContext(S source, String input, Map<String, Object> arguments, List<CommandNode<S>> nodes,
                          CommandNode<S> rootNode, Command<S> command, CommandContext<S> child,
                          CommandNode<S> redirect, StringRange range) {
        this.source = source; this.input = input == null ? "" : input;
        this.arguments = arguments == null ? new HashMap<>() : new HashMap<>(arguments);
        this.nodes = nodes == null ? List.of() : List.copyOf(nodes); this.rootNode = rootNode; this.command = command; this.child = child; this.redirect = redirect;
        this.range = range == null ? StringRange.at(0) : range;
    }
    public S getSource() { return source; }
    public String getInput() { return input; }
    public <V> V getArgument(String name, Class<V> type) {
        Object value = arguments.get(name); return value == null ? null : type.cast(value);
    }
    public <V> V getArgumentOrDefault(String name, Class<V> type, V fallback) { V value = getArgument(name, type); return value == null ? fallback : value; }
    public Map<String, Object> getArguments() { return Map.copyOf(arguments); }
    public void putArgument(String name, Object value) { if (name != null) arguments.put(name, value); }
    public List<CommandNode<S>> getNodes() { return nodes; }
    public boolean hasNodes() { return !nodes.isEmpty(); }
    public CommandNode<S> getRootNode() { return rootNode; }
    public Command<S> getCommand() { return command; }
    public CommandContext<S> getChild() { return child; }
    public CommandContext<S> getLastChild() { CommandContext<S> current = child; while (current != null && current.child != null) current = current.child; return current; }
    public CommandNode<S> getRedirect() { return redirect; }
    public StringRange getRange() { return range; }
    public CommandContext<S> copyFor(S newSource) { return new CommandContext<>(newSource, input, arguments, nodes, rootNode, command, child, redirect, range); }
}
