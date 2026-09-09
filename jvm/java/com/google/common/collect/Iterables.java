package com.google.common.collect;

import com.google.common.base.Predicate;
import java.lang.reflect.Array;
import java.util.ArrayList;
import java.util.List;

/** Minimal Guava iterable helper surface used by collision mixins. */
public final class Iterables {
    private Iterables() { }
    public static <T> boolean any(Iterable<T> values, Predicate<? super T> predicate) {
        if (values == null || predicate == null) return false;
        for (T value : values) if (predicate.apply(value)) return true;
        return false;
    }

    /** Guava-compatible typed materialization helper used by configuration mods. */
    public static <T> T[] toArray(Iterable<? extends T> values, T[] array) {
        if (values == null || array == null) throw new NullPointerException();
        List<T> copy = new ArrayList<>();
        for (T value : values) copy.add(value);
        T[] result = copy.size() <= array.length
            ? array
            : (T[]) Array.newInstance(array.getClass().getComponentType(), copy.size());
        for (int index = 0; index < copy.size(); index++) result[index] = copy.get(index);
        if (result.length > copy.size()) result[copy.size()] = null;
        return result;
    }

    @SuppressWarnings("unchecked")
    public static <T> T[] toArray(Iterable<? extends T> values) {
        if (values == null) throw new NullPointerException();
        List<T> copy = new ArrayList<>();
        for (T value : values) copy.add(value);
        return (T[]) copy.toArray();
    }
}
