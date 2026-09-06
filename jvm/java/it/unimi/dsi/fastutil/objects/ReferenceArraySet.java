package it.unimi.dsi.fastutil.objects;

import java.util.Collection;
import java.util.LinkedHashSet;
import java.util.Set;

/** Compact reference-set compatibility surface used by Lithium's fluid cache. */
public class ReferenceArraySet<V> extends LinkedHashSet<V> {
    public ReferenceArraySet() { }
    public ReferenceArraySet(Set<? extends V> values) {
        if (values != null) addAll(values);
    }
    public ReferenceArraySet(Collection<? extends V> values) {
        if (values != null) addAll(values);
    }
}
