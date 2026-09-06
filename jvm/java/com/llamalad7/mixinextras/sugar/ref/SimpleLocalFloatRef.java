package com.llamalad7.mixinextras.sugar.ref;

/** Backing object for {@link LocalFloatRef}. */
public final class SimpleLocalFloatRef implements LocalFloatRef {
    private float value;

    public SimpleLocalFloatRef(float value) { this.value = value; }
    @Override public float get() { return value; }
    @Override public void set(float value) { this.value = value; }
}
