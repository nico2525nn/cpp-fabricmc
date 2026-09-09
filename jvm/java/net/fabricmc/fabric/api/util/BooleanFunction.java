package net.fabricmc.fabric.api.util;

@FunctionalInterface
public interface BooleanFunction<R> {
    R apply(boolean value);
}
