package it.unimi.dsi.fastutil.objects;

import java.util.ArrayList;
import java.util.Collection;

/** Array-backed implementation for the embedded fastutil surface. */
public class ObjectArrayList<K> extends ArrayList<K> implements ObjectList<K> {
    public ObjectArrayList() { super(); }
    public ObjectArrayList(int expectedSize) { super(expectedSize); }
    public ObjectArrayList(Collection<? extends K> values) { super(values); }
}
