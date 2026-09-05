package com.mojang.brigadier;

import com.mojang.brigadier.arguments.StringArgumentType;
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
import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;

/**
 * A small but functional Brigadier dispatcher.
 *
 * The embedded server does not need Brigadier's parser internals, but mods do
 * rely on the public tree, argument, redirect, execution and suggestion
 * contracts. This implementation keeps parsing deterministic while preserving
 * the useful server-side behaviour of the real dispatcher.
 */
public final class CommandDispatcher<S> {
    public static final String ARGUMENT_SEPARATOR = " ";
    public static final char ARGUMENT_SEPARATOR_CHAR = ' ';

    private final RootCommandNode<S> root;
    private ResultConsumer<S> consumer = (context, success, result) -> { };

    public CommandDispatcher() { this(new RootCommandNode<>()); }
    public CommandDispatcher(RootCommandNode<S> root) {
        this.root = root == null ? new RootCommandNode<>() : root;
    }

    public LiteralCommandNode<S> register(LiteralArgumentBuilder<S> builder) {
        if (builder == null) throw new NullPointerException("builder");
        LiteralCommandNode<S> node = builder.build();
        root.addChild(node);
        return node;
    }

    public CommandNode<S> register(ArgumentBuilder<S, ?> builder) {
        if (builder == null) throw new NullPointerException("builder");
        CommandNode<S> node = builder.build();
        root.addChild(node);
        return node;
    }

    public int execute(String input, S source) throws CommandSyntaxException {
        return execute(parse(input, source));
    }

    public int execute(ParseResults<S> parse) throws CommandSyntaxException {
        if (parse == null || parse.getReader() == null) {
            throw new CommandSyntaxException("null command");
        }
        CommandContext<S> context = parse.getContext();
        try {
            ExecutionResult<S> result = executeNode(root, parse.getTokens(), 0,
                context, parse.getReader().getString(), 0);
            if (result == null) {
                throw syntax("Unknown or incomplete command", parse.getReader().getString(),
                    parse.getReader().getString().length());
            }
            consumer.onCommand(result.context, true, result.result);
            return result.result;
        } catch (CommandSyntaxException | RuntimeException failure) {
            consumer.onCommand(context, false, 0);
            throw failure;
        }
    }

    /** Parse without executing. Errors are retained as the deepest usable path. */
    public ParseResults<S> parse(String input, S source) throws CommandSyntaxException {
        String normalized = normalize(input);
        StringReader reader = new StringReader(normalized);
        List<Token> lexed = tokenize(normalized);
        List<String> tokens = new ArrayList<>();
        for (Token token : lexed) tokens.add(token.text);
        CommandContext<S> context = new CommandContext<>(source, normalized, Map.of(),
            List.of(), root, null, null, null, StringRange.at(0));
        PathResult<S> path = parsePath(root, 0, context, lexed, normalized, 0);
        reader.setCursor(normalized.length());
        if (path == null) return new ParseResults<>(reader, context, root, tokens);
        return new ParseResults<>(reader, path.context, path.node, tokens);
    }

    public CompletableFuture<Suggestions> getCompletionSuggestions(ParseResults<S> parse) {
        return getCompletionSuggestions(parse, parse == null || parse.getReader() == null
            ? 0 : parse.getReader().getCursor());
    }

    public CompletableFuture<Suggestions> getCompletionSuggestions(ParseResults<S> parse, int cursor) {
        if (parse == null || parse.getReader() == null) {
            return CompletableFuture.completedFuture(Suggestions.EMPTY);
        }
        String input = parse.getReader().getString();
        int safeCursor = Math.max(0, Math.min(cursor, input.length()));
        String prefix = input.substring(0, safeCursor);
        boolean afterWhitespace = !prefix.isEmpty()
            && Character.isWhitespace(prefix.charAt(prefix.length() - 1));
        List<Token> tokens;
        try {
            tokens = tokenize(prefix);
        } catch (CommandSyntaxException ignored) {
            tokens = List.of();
        }
        String current = afterWhitespace || tokens.isEmpty() ? "" : tokens.get(tokens.size() - 1).text;
        int completeCount = afterWhitespace ? tokens.size() : Math.max(0, tokens.size() - 1);
        int start = Math.max(0, safeCursor - current.length());

        CommandNode<S> node = root;
        CommandContext<S> context = new CommandContext<>(parse.getContext().getSource(), input,
            Map.of(), List.of(), root, null, null, null, StringRange.at(0));
        for (int index = 0; index < completeCount; index++) {
            Token token = tokens.get(index);
            CommandNode<S> selected = null;
            for (CommandNode<S> child : node.getChildren()) {
                if (!child.canUse(context.getSource())) continue;
                if (child instanceof LiteralCommandNode<S> literal
                    && literal.getLiteral().equals(token.text)) {
                    selected = child;
                    break;
                }
                if (child instanceof ArgumentCommandNode<S, ?> argument) {
                    try {
                        Object value = parseArgument(argument, token, tokens, index, input);
                        context.putArgument(argument.getName(), value);
                        selected = child;
                        break;
                    } catch (CommandSyntaxException ignored) { }
                }
            }
            if (selected == null) break;
            context.addNode(selected);
            node = selected;
        }

        SuggestionsBuilder literalBuilder = new SuggestionsBuilder(prefix, start);
        List<CompletableFuture<Suggestions>> futures = new ArrayList<>();
        for (CommandNode<S> child : node.getChildren()) {
            if (!child.canUse(context.getSource())) continue;
            if (child instanceof LiteralCommandNode<S> literal) {
                if (literal.getLiteral().startsWith(current)) literalBuilder.suggest(literal.getLiteral());
            } else if (child instanceof ArgumentCommandNode<S, ?> argument) {
                try {
                    futures.add(argument.listSuggestions(context,
                        new SuggestionsBuilder(prefix, start)));
                } catch (CommandSyntaxException ignored) { }
            }
        }
        List<Suggestions> values = new ArrayList<>();
        Suggestions literalSuggestions = literalBuilder.build();
        if (!literalSuggestions.isEmpty()) values.add(literalSuggestions);
        for (CompletableFuture<Suggestions> future : futures) {
            if (future == null) continue;
            try {
                Suggestions value = future.join();
                if (value != null && !value.isEmpty()) values.add(value);
            } catch (RuntimeException ignored) { }
        }
        return CompletableFuture.completedFuture(values.isEmpty()
            ? new Suggestions(StringRange.between(start, safeCursor), List.of())
            : Suggestions.merge(prefix, values));
    }

    public RootCommandNode<S> getRoot() { return root; }

    public Collection<LiteralCommandNode<S>> getRootNodes() {
        List<LiteralCommandNode<S>> result = new ArrayList<>();
        for (CommandNode<S> node : root.getChildren()) {
            if (node instanceof LiteralCommandNode<S> literal) result.add(literal);
        }
        return List.copyOf(result);
    }

    public void setConsumer(ResultConsumer<S> consumer) {
        this.consumer = consumer == null ? (context, success, result) -> { } : consumer;
    }

    public boolean hasCommand(String name) {
        String value = normalize(name);
        List<Token> tokens;
        try { tokens = tokenize(value); } catch (CommandSyntaxException ignored) { return false; }
        if (tokens.isEmpty()) return false;
        CommandNode<S> node = root;
        for (Token token : tokens) {
            CommandNode<S> next = node.getChild(token.text);
            if (next == null) {
                for (CommandNode<S> child : node.getChildren()) {
                    if (child instanceof ArgumentCommandNode<S, ?> argument
                        && argument.isValidInput(token.raw)) {
                        next = child;
                        break;
                    }
                }
            }
            if (next == null) return false;
            node = next;
        }
        return node.getCommand() != null || hasExecutableDescendant(node);
    }

    public List<String> getAllUsage(CommandNode<S> node, S source, boolean restricted) {
        if (node == null) return List.of();
        List<String> result = new ArrayList<>();
        collectUsage(node, source, restricted, "", result);
        return List.copyOf(result);
    }

    public Map<CommandNode<S>, String> getSmartUsage(CommandNode<S> node, S source) {
        if (node == null) return Map.of();
        Map<CommandNode<S>, String> result = new LinkedHashMap<>();
        for (CommandNode<S> child : node.getChildren()) {
            if (child.canUse(source)) result.put(child, child.getUsageText());
        }
        return Map.copyOf(result);
    }

    public String[] getPath(CommandNode<S> target) {
        if (target == null || target == root) return new String[0];
        List<String> path = new ArrayList<>();
        if (!findPath(root, target, path)) return new String[0];
        return path.toArray(String[]::new);
    }

    public CommandNode<S> findNode(String[] path) {
        CommandNode<S> node = root;
        if (path == null) return node;
        for (String part : path) {
            node = node.getChild(part);
            if (node == null) return null;
        }
        return node;
    }

    public void findAmbiguities(Object consumer) { }

    private PathResult<S> parsePath(CommandNode<S> node, int index,
                                    CommandContext<S> context, List<Token> tokens,
                                    String input, int depth) {
        if (depth > 64) return new PathResult<>(node, context, index);
        PathResult<S> best = new PathResult<>(node, context, index);
        if (index >= tokens.size()) return best;
        for (CommandNode<S> child : node.getChildren()) {
            if (!child.canUse(context.getSource())) continue;
            Token token = tokens.get(index);
            try {
                CommandContext<S> next = copyContext(context);
                int nextIndex;
                if (child instanceof LiteralCommandNode<S> literal) {
                    if (!literal.getLiteral().equals(token.text)) continue;
                    nextIndex = index + 1;
                } else if (child instanceof ArgumentCommandNode<S, ?> argument) {
                    Object value = parseArgument(argument, token, tokens, index, input);
                    next.putArgument(argument.getName(), value);
                    nextIndex = argument.getType() instanceof StringArgumentType string
                        && string.isGreedy() ? tokens.size() : index + 1;
                } else continue;
                next.addNode(child);
                PathResult<S> candidate = parsePath(child, nextIndex, next, tokens, input, depth + 1);
                if (candidate.index > best.index
                    || (candidate.index == best.index && candidate.node.getCommand() != null)) {
                    best = candidate;
                }
            } catch (CommandSyntaxException | RuntimeException ignored) { }
        }
        return best;
    }

    private ExecutionResult<S> executeNode(CommandNode<S> node, List<String> tokens, int index,
                                           CommandContext<S> context, String input, int depth)
            throws CommandSyntaxException {
        if (depth > 64) throw syntax("Command redirect cycle", input, input.length());
        if (node.getRedirect() != null) return executeRedirect(node, tokens, index, context, input, depth);
        if (index >= tokens.size()) {
            if (node.getCommand() == null) return null;
            context.setCommand(node.getCommand());
            return new ExecutionResult<>(node.getCommand().run(context), context);
        }
        CommandSyntaxException lastFailure = null;
        for (CommandNode<S> child : node.getChildren()) {
            if (!child.canUse(context.getSource())) continue;
            CommandContext<S> next = copyContext(context);
            int nextIndex;
            try {
                if (child instanceof LiteralCommandNode<S> literal) {
                    if (!literal.getLiteral().equals(tokens.get(index))) continue;
                    nextIndex = index + 1;
                } else if (child instanceof ArgumentCommandNode<S, ?> argument) {
                    int end = index + 1;
                    String value = tokens.get(index);
                    if (argument.getType() instanceof StringArgumentType string && string.isGreedy()) {
                        value = String.join(" ", tokens.subList(index, tokens.size()));
                        end = tokens.size();
                    }
                    Object parsed = argument.getType().parse(new StringReader(value));
                    next.putArgument(argument.getName(), parsed);
                    nextIndex = end;
                } else continue;
                next.addNode(child);
                ExecutionResult<S> result = executeNode(child, tokens, nextIndex, next, input, depth + 1);
                if (result != null) return result;
            } catch (CommandSyntaxException failure) {
                lastFailure = failure;
            }
        }
        if (lastFailure != null) throw lastFailure;
        return null;
    }

    private ExecutionResult<S> executeRedirect(CommandNode<S> node, List<String> tokens, int index,
                                               CommandContext<S> context, String input, int depth)
            throws CommandSyntaxException {
        Collection<S> sources;
        if (node.getRedirectModifier() == null) sources = List.of(context.getSource());
        else sources = node.getRedirectModifier().apply(context);
        if (sources == null || sources.isEmpty()) return null;
        int total = 0;
        ExecutionResult<S> first = null;
        for (S source : sources) {
            ExecutionResult<S> result = executeNode(node.getRedirect(), tokens, index,
                context.copyFor(source), input, depth + 1);
            if (result == null) continue;
            if (first == null) first = result;
            total += result.result;
            if (!node.isFork()) break;
        }
        return first == null ? null : new ExecutionResult<>(node.isFork() ? total : first.result, first.context);
    }

    private Object parseArgument(ArgumentCommandNode<S, ?> argument, Token token,
                                 List<Token> tokens, int index, String input)
            throws CommandSyntaxException {
        String value = token.raw;
        if (argument.getType() instanceof StringArgumentType string && string.isGreedy()) {
            value = input.substring(Math.max(0, token.start));
        }
        StringReader reader = new StringReader(value);
        Object result = argument.getType().parse(reader);
        reader.skipWhitespace();
        if (reader.canRead()) throw syntax("Trailing data in argument", input, token.end);
        return result;
    }

    private CommandContext<S> copyContext(CommandContext<S> context) {
        return new CommandContext<>(context.getSource(), context.getInput(), context.getArguments(),
            context.getNodes(), context.getRootNode(), context.getCommand(), context.getChild(),
            context.getRedirect(), context.getRange());
    }

    private boolean hasExecutableDescendant(CommandNode<S> node) {
        for (CommandNode<S> child : node.getChildren()) {
            if (child.getCommand() != null || hasExecutableDescendant(child)) return true;
        }
        return false;
    }

    private void collectUsage(CommandNode<S> node, S source, boolean restricted,
                              String prefix, List<String> output) {
        if (node.getCommand() != null && (!restricted || node.canUse(source))) {
            output.add(prefix.isEmpty() ? node.getUsageText() : prefix);
        }
        for (CommandNode<S> child : node.getChildren()) {
            if (restricted && !child.canUse(source)) continue;
            String next = prefix.isEmpty() ? child.getUsageText()
                : prefix + ARGUMENT_SEPARATOR + child.getUsageText();
            collectUsage(child, source, restricted, next, output);
        }
    }

    private boolean findPath(CommandNode<S> current, CommandNode<S> target, List<String> path) {
        for (CommandNode<S> child : current.getChildren()) {
            path.add(child.getName());
            if (child == target || findPath(child, target, path)) return true;
            path.remove(path.size() - 1);
        }
        return false;
    }

    private static String normalize(String input) {
        String value = input == null ? "" : input;
        if (value.startsWith("/")) value = value.substring(1);
        int start = 0;
        while (start < value.length() && Character.isWhitespace(value.charAt(start))) start++;
        return value.substring(start);
    }

    private static List<Token> tokenize(String input) throws CommandSyntaxException {
        if (input == null || input.isEmpty()) return List.of();
        List<Token> result = new ArrayList<>();
        int index = 0;
        while (index < input.length()) {
            while (index < input.length() && Character.isWhitespace(input.charAt(index))) index++;
            if (index >= input.length()) break;
            int start = index;
            StringBuilder decoded = new StringBuilder();
            char quote = 0;
            boolean escaped = false;
            while (index < input.length()) {
                char value = input.charAt(index++);
                if (escaped) {
                    decoded.append(value);
                    escaped = false;
                } else if (quote != 0 && value == '\\') {
                    escaped = true;
                } else if (quote != 0 && value == quote) {
                    quote = 0;
                } else if (quote == 0 && (value == '"' || value == '\'')) {
                    quote = value;
                } else if (quote == 0 && Character.isWhitespace(value)) {
                    break;
                } else {
                    decoded.append(value);
                }
            }
            if (escaped || quote != 0) {
                throw new CommandSyntaxException("Unclosed quoted string", input, index);
            }
            result.add(new Token(decoded.toString(), input.substring(start, index).stripTrailing(), start, index));
        }
        return result;
    }

    private static CommandSyntaxException syntax(String message, String input, int cursor) {
        return new CommandSyntaxException(message, input,
            Math.max(0, Math.min(cursor, input == null ? 0 : input.length())));
    }

    private static final class Token {
        private final String text;
        private final String raw;
        private final int start;
        private final int end;
        private Token(String text, String raw, int start, int end) {
            this.text = text; this.raw = raw; this.start = start; this.end = end;
        }
    }

    private static final class PathResult<S> {
        private final CommandNode<S> node;
        private final CommandContext<S> context;
        private final int index;
        private PathResult(CommandNode<S> node, CommandContext<S> context, int index) {
            this.node = node; this.context = context; this.index = index;
        }
    }

    private static final class ExecutionResult<S> {
        private final int result;
        private final CommandContext<S> context;
        private ExecutionResult(int result, CommandContext<S> context) {
            this.result = result; this.context = context;
        }
    }
}
