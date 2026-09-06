package com.llamalad7.mixinextras.sugar.ref;

/** Small allocation-free-after-construction backing object for {@link LocalRef}. */
public final class SimpleLocalRef implements LocalRef<Object> {
    private Object value;

    public SimpleLocalRef(Object value) { this.value = value; }
    @Override public Object get() { return value; }
    @Override public void set(Object value) { this.value = value; }
}
