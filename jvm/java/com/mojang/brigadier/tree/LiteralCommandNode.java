package com.mojang.brigadier.tree;

import com.mojang.brigadier.Command;
import com.mojang.brigadier.RedirectModifier;
import java.util.function.Predicate;

public class LiteralCommandNode<S> extends CommandNode<S> {
    private final String literal;
    public LiteralCommandNode(String literal, Command<S> command,
                              Predicate<S> requirement, CommandNode<S> redirect,
                              Object redirectModifier, boolean forks) {
        super(command, requirement, redirect, redirectModifier instanceof RedirectModifier<?> modifier ? (RedirectModifier<S>) modifier : null, forks);
        this.literal = literal == null ? "" : literal;
    }
    public LiteralCommandNode(String literal, Command<S> command, Predicate<S> requirement,
                              CommandNode<S> redirect, com.mojang.brigadier.SingleRedirectModifier<S> modifier, boolean forks) {
        this(literal, command, requirement, redirect, (Object) wrap(modifier), forks);
    }
    private static <S> RedirectModifier<S> wrap(com.mojang.brigadier.SingleRedirectModifier<S> modifier) {
        return modifier == null ? null : context -> java.util.List.of(modifier.apply(context));
    }
    public String getLiteral() { return literal; }
    @Override public String getName() { return literal; }
    @Override public String getUsageText() { return literal; }
    @Override public java.util.Collection<String> getExamples() { return java.util.List.of(literal); }
}
