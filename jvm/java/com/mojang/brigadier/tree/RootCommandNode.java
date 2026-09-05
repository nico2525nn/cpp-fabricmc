package com.mojang.brigadier.tree;

public final class RootCommandNode<S> extends CommandNode<S> {
    public RootCommandNode() { super(null, source -> true); }
    @Override public String getName() { return ""; }
}
