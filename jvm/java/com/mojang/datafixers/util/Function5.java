package com.mojang.datafixers.util;

@FunctionalInterface
public interface Function5<A, B, C, D, E, R> {
    R apply(A first, B second, C third, D fourth, E fifth);
}
