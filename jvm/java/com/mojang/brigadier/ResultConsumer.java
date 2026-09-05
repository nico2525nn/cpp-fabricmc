package com.mojang.brigadier;

@FunctionalInterface
public interface ResultConsumer<S> {
    void onCommand(com.mojang.brigadier.context.CommandContext<S> context,
                   boolean success, int result);
}
