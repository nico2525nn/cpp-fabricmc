package com.mojang.datafixers.util;

@FunctionalInterface
public interface Function3<A, B, C, R> {
    R apply(A first, B second, C third);
}
