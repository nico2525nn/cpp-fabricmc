package com.mojang.brigadier.tree;

import com.mojang.brigadier.Command;
import java.util.function.Predicate;

public class LiteralCommandNode<S> extends CommandNode<S> {
    private final String literal;
    public LiteralCommandNode(String literal, Command<S> command,
                              Predicate<S> requirement, CommandNode<S> redirect,
                              Object redirectModifier, boolean forks) {
        super(command, requirement);
        this.literal = literal == null ? "" : literal;
    }
    public String getLiteral() { return literal; }
    @Override public String getName() { return literal; }
    @Override public String getUsageText() { return literal; }
}
