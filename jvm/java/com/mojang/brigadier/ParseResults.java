package com.mojang.brigadier;

import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.tree.CommandNode;
import java.util.List;

public final class ParseResults<S> {
    private final StringReader reader;
    private final CommandContext<S> context;
    private final CommandNode<S> node;
    private final List<String> tokens;
    public ParseResults(StringReader reader, CommandContext<S> context, CommandNode<S> node, List<String> tokens) { this.reader = reader; this.context = context; this.node = node; this.tokens = tokens == null ? List.of() : List.copyOf(tokens); }
    public StringReader getReader() { return reader; }
    public CommandContext<S> getContext() { return context; }
    public CommandNode<S> getNode() { return node; }
    public List<String> getTokens() { return tokens; }
}
