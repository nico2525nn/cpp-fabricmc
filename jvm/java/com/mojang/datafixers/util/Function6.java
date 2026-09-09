package com.mojang.datafixers.util;

@FunctionalInterface
public interface Function6<A, B, C, D, E, F, R> {
    R apply(A first, B second, C third, D fourth, E fifth, F sixth);
}
