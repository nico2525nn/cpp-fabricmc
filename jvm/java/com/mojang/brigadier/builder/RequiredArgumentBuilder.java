package com.mojang.brigadier.builder;

import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.tree.ArgumentCommandNode;
import com.mojang.brigadier.suggestion.SuggestionProvider;

public class RequiredArgumentBuilder<S, T> extends ArgumentBuilder<S, RequiredArgumentBuilder<S, T>> {
    private final String name;
    private final ArgumentType<T> type;
    private SuggestionProvider<S> suggestions;
    private RequiredArgumentBuilder(String name, ArgumentType<T> type) {
        this.name = name; this.type = type;
    }
    public static <S, T> RequiredArgumentBuilder<S, T> argument(String name, ArgumentType<T> type) {
        return new RequiredArgumentBuilder<>(name, type);
    }
    @Override public String getName() { return name; }
    public ArgumentType<T> getType() { return type; }
    public RequiredArgumentBuilder<S, T> suggests(SuggestionProvider<S> provider) {
        this.suggestions = provider; return this;
    }
    public SuggestionProvider<S> getSuggestionsProvider() { return suggestions; }
    @Override public ArgumentCommandNode<S, T> build() {
        ArgumentCommandNode<S, T> node = new ArgumentCommandNode<>(
            name, type, command, requirement, null, null, false, suggestions);
        for (ArgumentBuilder<S, ?> child : children) node.addChild(child.build());
        return node;
    }
}
