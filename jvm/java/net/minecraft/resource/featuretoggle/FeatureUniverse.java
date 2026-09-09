package net.minecraft.resource.featuretoggle;

import java.util.Objects;

/** Namespace for feature flags. */
public final class FeatureUniverse {
    public static final FeatureUniverse VANILLA = new FeatureUniverse("vanilla");
    private final String name;

    public FeatureUniverse(String name) { this.name = Objects.requireNonNull(name, "name"); }
    public String getName() { return name; }
    @Override public boolean equals(Object other) {
        return other instanceof FeatureUniverse universe && name.equals(universe.name);
    }
    @Override public int hashCode() { return name.hashCode(); }
    @Override public String toString() { return name; }
}
