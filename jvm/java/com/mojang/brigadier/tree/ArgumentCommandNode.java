package com.mojang.brigadier.tree;

import com.mojang.brigadier.Command;
import com.mojang.brigadier.RedirectModifier;
import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;
import com.mojang.brigadier.suggestion.Suggestions;
import com.mojang.brigadier.suggestion.SuggestionsBuilder;
import com.mojang.brigadier.suggestion.SuggestionProvider;
import java.util.concurrent.CompletableFuture;
import java.util.function.Predicate;

public class ArgumentCommandNode<S, T> extends CommandNode<S> {
    private final String name;
    private final ArgumentType<T> type;
    private final SuggestionProvider<S> suggestions;
    public ArgumentCommandNode(String name, ArgumentType<T> type, Command<S> command,
                               Predicate<S> requirement, CommandNode<S> redirect,
                               Object redirectModifier, boolean forks, Object suggestions) {
        super(command, requirement, redirect, redirectModifier instanceof RedirectModifier<?> modifier ? (RedirectModifier<S>) modifier : null, forks);
        this.name = name == null ? "" : name;
        this.type = type;
        this.suggestions = suggestions instanceof SuggestionProvider<?> provider ? (SuggestionProvider<S>) provider : null;
    }
    public String getName() { return name; }
    public ArgumentType<T> getType() { return type; }
    public SuggestionProvider<S> getCustomSuggestions() { return suggestions; }
    public boolean isValidInput(String input) {
        try { if (type == null) return false; type.parse(new com.mojang.brigadier.StringReader(input)); return true; }
        catch (CommandSyntaxException | RuntimeException ignored) { return false; }
    }
    public CompletableFuture<Suggestions> listSuggestions(CommandContext<S> context, SuggestionsBuilder builder)
            throws CommandSyntaxException {
        if (suggestions != null) return suggestions.getSuggestions(context, builder);
        return type == null ? CompletableFuture.completedFuture(builder.build()) : type.listSuggestions(context, builder);
    }
    @Override public String getUsageText() { return "<" + name + ">"; }
    @Override public java.util.Collection<String> getExamples() { return type == null ? java.util.List.of() : type.getExamples(); }
}
