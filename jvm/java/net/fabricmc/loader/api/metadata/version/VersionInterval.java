package net.fabricmc.loader.api.metadata.version;

import java.util.List;
import net.fabricmc.loader.api.Version;

/** Minimal immutable version interval contract for metadata consumers. */
public interface VersionInterval {
    VersionInterval INFINITE = new VersionInterval() {
        @Override public boolean isSemantic() { return true; }
        @Override public Version getMin() { return null; }
        @Override public boolean isMinInclusive() { return true; }
        @Override public Version getMax() { return null; }
        @Override public boolean isMaxInclusive() { return true; }
    };

    boolean isSemantic();
    Version getMin();
    boolean isMinInclusive();
    Version getMax();
    boolean isMaxInclusive();

    default VersionInterval and(VersionInterval other) { return this; }
    default List<VersionInterval> or(java.util.Collection<VersionInterval> others) { return List.of(this); }
    default List<VersionInterval> not() { return List.of(); }
    static VersionInterval and(VersionInterval left, VersionInterval right) { return left; }
    static List<VersionInterval> and(java.util.Collection<VersionInterval> left,
                                     java.util.Collection<VersionInterval> right) {
        return left == null || left.isEmpty() ? List.of() : List.copyOf(left);
    }
    static List<VersionInterval> or(java.util.Collection<VersionInterval> values,
                                    VersionInterval value) {
        java.util.ArrayList<VersionInterval> result = new java.util.ArrayList<>();
        if (values != null) result.addAll(values);
        if (value != null) result.add(value);
        return List.copyOf(result);
    }
    static List<VersionInterval> not(VersionInterval value) { return List.of(); }
    static List<VersionInterval> not(java.util.Collection<VersionInterval> value) { return List.of(); }
}
