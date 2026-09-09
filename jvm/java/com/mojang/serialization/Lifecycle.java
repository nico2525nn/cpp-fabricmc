package com.mojang.serialization;

/**
 * Minimal immutable lifecycle value used by the 1.21.4 registry constructors.
 * The native server does not serialize this marker, but real Fabric registry
 * code passes it through constructor and registration boundaries.
 */
public final class Lifecycle {
    private static final Lifecycle STABLE = new Lifecycle("stable");
    private static final Lifecycle EXPERIMENTAL = new Lifecycle("experimental");
    private final String name;

    private Lifecycle(String name) { this.name = name; }

    public static Lifecycle stable() { return STABLE; }
    public static Lifecycle experimental() { return EXPERIMENTAL; }
    public Lifecycle add(Lifecycle other) {
        return this == EXPERIMENTAL || other == EXPERIMENTAL ? EXPERIMENTAL : STABLE;
    }
    @Override public String toString() { return name; }
}
