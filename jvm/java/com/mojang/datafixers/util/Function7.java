package com.mojang.datafixers.util;

@FunctionalInterface
public interface Function7<A, B, C, D, E, F, G, R> {
    R apply(A first, B second, C third, D fourth, E fifth, F sixth, G seventh);
}
