package com.llamalad7.mixinextras.sugar.ref;

/** Backing object for {@link LocalBooleanRef}. */
public final class SimpleLocalBooleanRef implements LocalBooleanRef {
    private boolean value;

    public SimpleLocalBooleanRef(boolean value) { this.value = value; }
    @Override public boolean get() { return value; }
    @Override public void set(boolean value) { this.value = value; }
}
