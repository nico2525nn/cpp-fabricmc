package net.minecraft.resource.featuretoggle;

import java.util.Objects;
import java.util.Collection;

/** Small immutable feature-set value used by server-side recipe APIs. */
public final class FeatureSet {
    public static final FeatureSet EMPTY = new FeatureSet(0L);
    private final long featuresMask;
    private final FeatureUniverse universe;

    public FeatureSet() { this(FeatureUniverse.VANILLA, 0L); }
    public FeatureSet(long featuresMask) { this(FeatureUniverse.VANILLA, featuresMask); }
    public FeatureSet(FeatureUniverse universe, long featuresMask) {
        this.universe = universe == null ? FeatureUniverse.VANILLA : universe;
        this.featuresMask = featuresMask;
    }

    public static FeatureSet empty() { return EMPTY; }
    public static FeatureSet of(FeatureFlag feature) { return of(feature, new FeatureFlag[0]); }
    public static FeatureSet of(FeatureFlag first, FeatureFlag... rest) {
        if (first == null) return EMPTY;
        long mask = first.mask();
        if (rest != null) for (FeatureFlag feature : rest)
            if (feature != null && feature.universe().equals(first.universe())) mask |= feature.mask();
        return new FeatureSet(first.universe(), mask);
    }
    public static FeatureSet of(FeatureUniverse universe, Collection<FeatureFlag> features) {
        long mask = 0L;
        if (features != null) for (FeatureFlag feature : features)
            if (feature != null) mask |= feature.mask();
        return new FeatureSet(universe, mask);
    }

    public boolean isEmpty() { return featuresMask == 0L; }
    public boolean contains(Object feature) {
        return feature instanceof FeatureFlag flag
            && universe.equals(flag.universe()) && (featuresMask & flag.mask()) != 0L;
    }
    public boolean isSubsetOf(FeatureSet features) {
        return features != null && universe.equals(features.universe)
            && (featuresMask & ~features.featuresMask) == 0L;
    }
    public boolean intersects(FeatureSet features) {
        return features != null && universe.equals(features.universe)
            && (featuresMask & features.featuresMask) != 0L;
    }
    public FeatureSet subtract(FeatureSet features) {
        return features == null || !universe.equals(features.universe)
            ? this : new FeatureSet(universe, featuresMask & ~features.featuresMask);
    }
    public FeatureSet combine(FeatureSet features) {
        if (features == null || !universe.equals(features.universe)) return this;
        return new FeatureSet(universe, featuresMask | features.featuresMask);
    }

    @Override public boolean equals(Object other) {
        return other instanceof FeatureSet set
            && universe.equals(set.universe) && featuresMask == set.featuresMask;
    }
    @Override public int hashCode() { return Objects.hash(universe, featuresMask); }
}
