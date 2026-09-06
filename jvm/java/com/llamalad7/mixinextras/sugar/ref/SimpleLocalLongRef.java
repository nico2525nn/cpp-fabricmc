package com.llamalad7.mixinextras.sugar.ref;

/** Backing object for {@link LocalLongRef}. */
public final class SimpleLocalLongRef implements LocalLongRef {
    private long value;

    public SimpleLocalLongRef(long value) { this.value = value; }
    @Override public long get() { return value; }
    @Override public void set(long value) { this.value = value; }
}
