package com.llamalad7.mixinextras.sugar.ref;

/** Backing object for {@link LocalDoubleRef}. */
public final class SimpleLocalDoubleRef implements LocalDoubleRef {
    private double value;

    public SimpleLocalDoubleRef(double value) { this.value = value; }
    @Override public double get() { return value; }
    @Override public void set(double value) { this.value = value; }
}
