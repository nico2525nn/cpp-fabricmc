package com.mojang.brigadier;

import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.builder.ArgumentBuilder;
import com.mojang.brigadier.builder.LiteralArgumentBuilder;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.context.StringRange;
import com.mojang.brigadier.exceptions.CommandSyntaxException;
import com.mojang.brigadier.suggestion.Suggestions;
import com.mojang.brigadier.suggestion.SuggestionsBuilder;
import com.mojang.brigadier.tree.ArgumentCommandNode;
import com.mojang.brigadier.tree.CommandNode;
import com.mojang.brigadier.tree.LiteralCommandNode;
import com.mojang.brigadier.tree.RootCommandNode;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.function.BiConsumer;

/** Deterministic Brigadier-compatible command tree for the embedded runtime. */
public final class CommandDispatcher<S> {
    public static final String ARGUMENT_SEPARATOR = " ";
    private final RootCommandNode<S> root;
    private BiConsumer<CommandContext<S>, Boolean> consumer = (context, success) -> { };

    public CommandDispatcher() { this(new RootCommandNode<>()); }
    public CommandDispatcher(RootCommandNode<S> root) { this.root = root == null ? new RootCommandNode<>() : root; }
    public LiteralCommandNode<S> register(LiteralArgumentBuilder<S> builder) {
        if (builder == null) throw new NullPointerException("builder");
        LiteralCommandNode<S> node = builder.build(); root.addChild(node); return node;
    }
    public CommandNode<S> register(ArgumentBuilder<S, ?> builder) {
        if (builder == null) throw new NullPointerException("builder");
        CommandNode<S> node = builder.build(); root.addChild(node); return node;
    }
    public int execute(String input, S source) throws CommandSyntaxException { return execute(parse(input, source)); }
    public int execute(ParseResults<S> parse) throws CommandSyntaxException {
        if (parse == null || parse.getReader() == null) throw new CommandSyntaxException("null command");
        CommandContext<S> context = parse.getContext();
        int result;
        try {
            // ParseResults points at the deepest node for completion/diagnostics,
            // while execution must traverse the command tree from its root.
            result = executeNode(root, parse.getTokens(), 0, context);
            if (result == Integer.MIN_VALUE) throw new CommandSyntaxException("Unknown or incomplete command", parse.getReader(), parse.getReader().getCursor());
            consumer.accept(context, true); return result;
        } catch (CommandSyntaxException | RuntimeException failure) {
            consumer.accept(context, false);
            if (failure instanceof CommandSyntaxException syntax) throw syntax;
            throw failure;
        }
    }
    public ParseResults<S> parse(String input, S source) throws CommandSyntaxException {
        String normalized = input == null ? "" : input.trim();
        if (normalized.startsWith("/")) normalized = normalized.substring(1).trim();
        StringReader reader = new StringReader(normalized);
        if (normalized.isEmpty()) throw new CommandSyntaxException("empty command", reader, 0);
        String[] tokens = tokenize(normalized);
        CommandNode<S> node = root;
        CommandContext<S> context = new CommandContext<>(source, normalized);
        int index = 0;
        while (index < tokens.length) {
            CommandNode<S> selected = select(node, tokens[index], source, context);
            if (selected == null) break;
            node = selected;
            if (selected instanceof ArgumentCommandNode<S, ?> argument) {
                StringReader valueReader = new StringReader(tokens[index]);
                Object value = argument.getType().parse(valueReader);
                context.putArgument(argument.getName(), value);
            }
            index++;
        }
        return new ParseResults<>(reader, context, node, Arrays.asList(tokens));
    }
    public CompletableFuture<Suggestions> getCompletionSuggestions(ParseResults<S> parse) {
        return getCompletionSuggestions(parse, parse == null ? 0 : parse.getReader().getCursor());
    }
    public CompletableFuture<Suggestions> getCompletionSuggestions(ParseResults<S> parse, int cursor) {
        if (parse == null) return CompletableFuture.completedFuture(Suggestions.EMPTY);
        String input = parse.getReader().getString(); String prefix = input.substring(0, Math.min(cursor, input.length()));
        String current = prefix.substring(prefix.lastIndexOf(' ') + 1);
        SuggestionsBuilder builder = new SuggestionsBuilder(input, prefix.length() - current.length());
        for (CommandNode<S> child : parse.getNode().getChildren()) {
            if (child.getName().startsWith(current)) builder.suggest(child.getName());
        }
        return CompletableFuture.completedFuture(builder.build());
    }
    public RootCommandNode<S> getRoot() { return root; }
    public Collection<LiteralCommandNode<S>> getRootNodes() {
        List<LiteralCommandNode<S>> result = new ArrayList<>();
        for (CommandNode<S> node : root.getChildren()) if (node instanceof LiteralCommandNode<S> literal) result.add(literal);
        return List.copyOf(result);
    }
    public void setConsumer(ResultConsumer<S> consumer) {
        this.consumer = consumer == null ? (context, success) -> { } : (context, success) -> consumer.onCommand(context, success, success ? Command.SINGLE_SUCCESS : 0);
    }
    public boolean hasCommand(String name) {
        String value = name == null ? "" : name.trim(); if (value.startsWith("/")) value = value.substring(1).trim();
        int end = 0; while (end < value.length() && !Character.isWhitespace(value.charAt(end))) end++;
        return end > 0 && root.getChild(value.substring(0, end)) != null;
    }
    public List<String> getAllUsage(CommandNode<S> node, S source, boolean restricted) { return node == null ? List.of() : List.of(node.getUsageText()); }
    public java.util.Map<CommandNode<S>, String> getSmartUsage(CommandNode<S> node, S source) { return node == null ? java.util.Map.of() : java.util.Map.of(node, node.getUsageText()); }
    public void findAmbiguities(Object consumer) { }

    private CommandNode<S> select(CommandNode<S> parent, String token, S source, CommandContext<S> context) {
        for (CommandNode<S> child : parent.getChildren()) if (child.canUse(source)) {
            if (child instanceof LiteralCommandNode<S> literal && literal.getLiteral().equals(token)) return child;
            if (child instanceof ArgumentCommandNode<?, ?>) return child;
        }
        return null;
    }
    private int executeNode(CommandNode<S> node, List<String> tokens, int index, CommandContext<S> context) throws CommandSyntaxException {
        if (index == tokens.size()) return node.getCommand() == null ? Integer.MIN_VALUE : node.getCommand().run(context);
        for (CommandNode<S> child : node.getChildren()) {
            if (!child.canUse(context.getSource())) continue;
            if (child instanceof LiteralCommandNode<S> literal) {
                if (!literal.getLiteral().equals(tokens.get(index))) continue;
                int result = executeNode(child, tokens, index + 1, context); if (result != Integer.MIN_VALUE) return result;
            } else if (child instanceof ArgumentCommandNode<S, ?> argument) {
                String token = tokens.get(index); if (argument.getType() instanceof com.mojang.brigadier.arguments.StringArgumentType text && text.isGreedy() && index + 1 < tokens.size()) token = String.join(" ", tokens.subList(index, tokens.size()));
                Object value = argument.getType().parse(new StringReader(token)); context.putArgument(argument.getName(), value);
                int next = argument.getType() instanceof com.mojang.brigadier.arguments.StringArgumentType text && text.isGreedy() ? tokens.size() : index + 1;
                int result = executeNode(child, tokens, next, context); if (result != Integer.MIN_VALUE) return result;
            }
        }
        return Integer.MIN_VALUE;
    }
    private static String[] tokenize(String input) {
        if (input.isEmpty()) return new String[0];
        List<String> tokens = new ArrayList<>(); StringBuilder current = new StringBuilder(); char quote = 0; boolean escape = false;
        for (int i = 0; i < input.length(); i++) { char c = input.charAt(i); if (escape) { current.append(c); escape = false; } else if (quote != 0 && c == '\\') escape = true; else if (quote != 0 && c == quote) quote = 0; else if (quote == 0 && (c == '"' || c == '\'')) quote = c; else if (quote == 0 && Character.isWhitespace(c)) { if (current.length() > 0) { tokens.add(current.toString()); current.setLength(0); } } else current.append(c); }
        if (current.length() > 0) tokens.add(current.toString()); return tokens.toArray(String[]::new);
    }
}
