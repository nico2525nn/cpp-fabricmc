package com.mojang.brigadier.builder;

import com.mojang.brigadier.tree.LiteralCommandNode;

public class LiteralArgumentBuilder<S> extends ArgumentBuilder<S, LiteralArgumentBuilder<S>> {
    private final String literal;
    protected LiteralArgumentBuilder(String literal) { this.literal = literal; }
    public static <S> LiteralArgumentBuilder<S> literal(String literal) { return new LiteralArgumentBuilder<>(literal); }
    @Override public String getName() { return literal; }
    public String getLiteral() { return literal; }
    @Override public LiteralCommandNode<S> build() {
        LiteralCommandNode<S> node = new LiteralCommandNode<>(
            literal, command, requirement, null, null, false);
        for (ArgumentBuilder<S, ?> child : children) node.addChild(child.build());
        return node;
    }
}
