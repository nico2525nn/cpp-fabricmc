package net.minecraft.util.collection;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Small weighted-value pool used in biome sound descriptors. */
public final class DataPool<T> {
    private final List<T> values;
    public DataPool() { this(List.of()); }
    public DataPool(List<T> values) { this.values = Collections.unmodifiableList(new ArrayList<>(values == null ? List.of() : values)); }
    public DataPool(T value) { this(value == null ? List.of() : List.of(value)); }
    public List<T> values() { return values; }
    public List<T> getEntries() { return values; }
    public static <T> DataPool<T> empty() { return new DataPool<>(); }
}
