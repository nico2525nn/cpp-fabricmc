package com.mojang.brigadier;

import com.mojang.brigadier.builder.ArgumentBuilder;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.tree.ArgumentCommandNode;
import com.mojang.brigadier.tree.CommandNode;
import com.mojang.brigadier.tree.LiteralCommandNode;
import com.mojang.brigadier.tree.RootCommandNode;
import com.mojang.brigadier.exceptions.CommandSyntaxException;
import java.util.ArrayList;
import java.util.List;
import com.mojang.brigadier.StringReader;

public final class CommandDispatcher<S> {
    private final List<ArgumentBuilder<S, ?>> roots = new ArrayList<>();
    private final RootCommandNode<S> root;

    public CommandDispatcher() { this(new RootCommandNode<>()); }
    public CommandDispatcher(RootCommandNode<S> root) {
        this.root = root == null ? new RootCommandNode<>() : root;
    }
    public LiteralCommandNode<S> register(
            com.mojang.brigadier.builder.LiteralArgumentBuilder<S> builder) {
        if (builder == null) return null;
        roots.add(builder);
        LiteralCommandNode<S> node = builder.build();
        root.addChild(node);
        return node;
    }
    public ArgumentBuilder<S, ?> register(ArgumentBuilder<S, ?> builder) {
        if (builder == null) return null;
        roots.add(builder);
        root.addChild(builder.build());
        return builder;
    }
    public int execute(String input, S source) throws CommandSyntaxException {
        String normalized = input == null ? "" : input.trim();
        if (normalized.startsWith("/")) normalized = normalized.substring(1).trim();
        if (normalized.isEmpty()) throw new CommandSyntaxException("empty command");
        String[] tokens = normalized.split("\\s+");
        for (CommandNode<S> candidate : root.getChildren()) {
            if (candidate instanceof LiteralCommandNode<S> literal &&
                literal.getLiteral().equals(tokens[0]) && candidate.canUse(source)) {
                CommandContext<S> context = new CommandContext<>(source, normalized);
                int result = executeNode(candidate, tokens, 1, context);
                if (result != Integer.MIN_VALUE) return result;
            }
        }
        throw new CommandSyntaxException("unknown command");
    }
    public List<ArgumentBuilder<S, ?>> getRootNodes() { return List.copyOf(roots); }
    public RootCommandNode<S> getRoot() { return root; }
    public boolean hasCommand(String name) {
        String value = name == null ? "" : name.trim();
        if (value.startsWith("/")) value = value.substring(1).trim();
        int end = 0;
        while (end < value.length() && !Character.isWhitespace(value.charAt(end))) ++end;
        if (end == 0) return false;
        return root.getChild(value.substring(0, end)) != null;
    }

    private int executeNode(CommandNode<S> node, String[] tokens, int index,
                            CommandContext<S> context) throws CommandSyntaxException {
        if (index == tokens.length) {
            if (node.getCommand() == null) return Integer.MIN_VALUE;
            return node.getCommand().run(context);
        }
        for (CommandNode<S> child : node.getChildren()) {
            if (!child.canUse(context.getSource())) continue;
            if (child instanceof LiteralCommandNode<S> literal) {
                if (!literal.getLiteral().equals(tokens[index])) continue;
                int result = executeNode(child, tokens, index + 1, context);
                if (result != Integer.MIN_VALUE) return result;
            } else if (child instanceof ArgumentCommandNode<S, ?> argument) {
                String token = tokens[index];
                if (argument.getType() instanceof com.mojang.brigadier.arguments.StringArgumentType stringType &&
                    stringType.isGreedy() && index + 1 < tokens.length)
                    token = String.join(" ", java.util.Arrays.copyOfRange(tokens, index, tokens.length));
                StringReader reader = new StringReader(token);
                Object value = argument.getType().parse(reader);
                context.putArgument(argument.getName(), value);
                int next = stringTypeIsGreedy(argument.getType()) ? tokens.length : index + 1;
                int result = executeNode(child, tokens, next, context);
                if (result != Integer.MIN_VALUE) return result;
            }
        }
        return Integer.MIN_VALUE;
    }

    private static boolean stringTypeIsGreedy(ArgumentType<?> type) {
        return type instanceof com.mojang.brigadier.arguments.StringArgumentType value &&
               value.isGreedy();
    }
}
