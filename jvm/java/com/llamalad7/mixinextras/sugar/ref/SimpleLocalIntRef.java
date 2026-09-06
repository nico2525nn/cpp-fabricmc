package com.llamalad7.mixinextras.sugar.ref;

/** Backing object for {@link LocalIntRef}. */
public final class SimpleLocalIntRef implements LocalIntRef {
    private int value;

    public SimpleLocalIntRef(int value) { this.value = value; }
    @Override public int get() { return value; }
    @Override public void set(int value) { this.value = value; }
}
