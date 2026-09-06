package net.minecraft.resource.featuretoggle;

import java.util.Objects;

/** Small immutable feature-set value used by server-side recipe APIs. */
public final class FeatureSet {
    public static final FeatureSet EMPTY = new FeatureSet(0L);
    private final long featuresMask;

    public FeatureSet() { this(0L); }
    public FeatureSet(long featuresMask) { this.featuresMask = featuresMask; }

    public boolean isEmpty() { return featuresMask == 0L; }
    public boolean contains(Object feature) { return false; }
    public boolean isSubsetOf(FeatureSet features) {
        return features != null && (featuresMask & ~features.featuresMask) == 0L;
    }
    public boolean intersects(FeatureSet features) {
        return features != null && (featuresMask & features.featuresMask) != 0L;
    }
    public FeatureSet subtract(FeatureSet features) {
        return new FeatureSet(featuresMask & ~(features == null ? 0L : features.featuresMask));
    }
    public FeatureSet combine(FeatureSet features) {
        return new FeatureSet(featuresMask | (features == null ? 0L : features.featuresMask));
    }

    @Override public boolean equals(Object other) {
        return other instanceof FeatureSet set && featuresMask == set.featuresMask;
    }
    @Override public int hashCode() { return Objects.hash(featuresMask); }
}
