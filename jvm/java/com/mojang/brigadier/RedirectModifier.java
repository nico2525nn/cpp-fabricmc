package com.mojang.brigadier;

@FunctionalInterface
public interface RedirectModifier<S> {
    java.util.Collection<S> apply(com.mojang.brigadier.context.CommandContext<S> context)
        throws com.mojang.brigadier.exceptions.CommandSyntaxException;
}
