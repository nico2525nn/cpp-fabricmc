package com.mojang.brigadier;

@FunctionalInterface
public interface SingleRedirectModifier<S> {
    S apply(com.mojang.brigadier.context.CommandContext<S> context)
        throws com.mojang.brigadier.exceptions.CommandSyntaxException;
}
