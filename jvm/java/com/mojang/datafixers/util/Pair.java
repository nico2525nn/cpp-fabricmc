package com.mojang.datafixers.util;

import java.util.Objects;

/** Small immutable pair used by vanilla's hoe/tilling registry. */
public final class Pair<F, S> {
    private final F first;
    private final S second;
    private Pair(F first, S second) { this.first = first; this.second = second; }
    public static <F, S> Pair<F, S> of(F first, S second) { return new Pair<>(first, second); }
    public F getFirst() { return first; }
    public S getSecond() { return second; }
    public F first() { return first; }
    public S second() { return second; }
    @Override public boolean equals(Object other) {
        return other instanceof Pair<?, ?> pair && Objects.equals(first, pair.first) && Objects.equals(second, pair.second);
    }
    @Override public int hashCode() { return Objects.hash(first, second); }
    @Override public String toString() { return "(" + first + "," + second + ")"; }
}
