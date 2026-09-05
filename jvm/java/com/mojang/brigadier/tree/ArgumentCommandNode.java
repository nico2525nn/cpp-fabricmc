package com.mojang.brigadier.tree;

import com.mojang.brigadier.Command;
import com.mojang.brigadier.RedirectModifier;
import com.mojang.brigadier.arguments.ArgumentType;
import java.util.function.Predicate;

public class ArgumentCommandNode<S, T> extends CommandNode<S> {
    private final String name;
    private final ArgumentType<T> type;
    public ArgumentCommandNode(String name, ArgumentType<T> type, Command<S> command,
                               Predicate<S> requirement, CommandNode<S> redirect,
                               Object redirectModifier, boolean forks, Object suggestions) {
        super(command, requirement, redirect, redirectModifier instanceof RedirectModifier<?> modifier ? (RedirectModifier<S>) modifier : null, forks);
        this.name = name == null ? "" : name;
        this.type = type;
    }
    public String getName() { return name; }
    public ArgumentType<T> getType() { return type; }
    @Override public String getUsageText() { return "<" + name + ">"; }
    @Override public java.util.Collection<String> getExamples() { return type == null ? java.util.List.of() : type.getExamples(); }
}
