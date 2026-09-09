package net.minecraft.resource.featuretoggle;

import java.util.Objects;

/** A single bit in a feature universe. */
public final class FeatureFlag {
    private final FeatureUniverse universe;
    private final long mask;

    public FeatureFlag(FeatureUniverse universe, int id) {
        this.universe = Objects.requireNonNull(universe, "universe");
        if (id < 0 || id >= Long.SIZE) throw new IllegalArgumentException("feature id out of range");
        this.mask = 1L << id;
    }
    FeatureUniverse universe() { return universe; }
    long mask() { return mask; }
    public FeatureUniverse getUniverse() { return universe; }
    @Override public boolean equals(Object other) {
        return other instanceof FeatureFlag flag
            && universe.equals(flag.universe) && mask == flag.mask;
    }
    @Override public int hashCode() { return Objects.hash(universe, mask); }
}
