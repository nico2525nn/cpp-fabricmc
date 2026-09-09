package com.mojang.datafixers.util;

@FunctionalInterface
public interface Function4<A, B, C, D, R> {
    R apply(A first, B second, C third, D fourth);
}
