package net.minecraft.util.collection;

import java.util.AbstractList;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;
import java.util.function.BiConsumer;
import java.util.function.IntFunction;

/** ArrayList-compatible list with a fixed default value, as used by inventories. */
public class DefaultedList<T> extends AbstractList<T> {
    private final List<T> values;
    private final T defaultValue;
    private final BiConsumer<Integer, T> setter;
    private final IntFunction<T> loader;

    private DefaultedList(int size, T defaultValue) {
        this.defaultValue = defaultValue;
        this.values = new ArrayList<>(Collections.nCopies(Math.max(0, size), defaultValue));
        this.setter = null;
        this.loader = null;
    }

    private DefaultedList(int size, T defaultValue, IntFunction<T> loader, BiConsumer<Integer, T> setter) {
        this.defaultValue = defaultValue;
        this.values = new ArrayList<>(Collections.nCopies(Math.max(0, size), defaultValue));
        this.loader = loader;
        this.setter = setter;
    }

    public static <T> DefaultedList<T> ofSize(int size, T defaultValue) {
        return new DefaultedList<>(size, defaultValue);
    }
    public static <T> DefaultedList<T> of() { return new DefaultedList<>(0, null); }
    public static <T> DefaultedList<T> ofSize(int size, T defaultValue, IntFunction<T> loader,
                                               BiConsumer<Integer, T> setter) {
        return new DefaultedList<>(size, defaultValue, loader, setter);
    }
    public T getDefaultValue() { return defaultValue; }

    @Override public T get(int index) {
        range(index);
        if (loader != null) {
            T value = loader.apply(index);
            return value == null ? defaultValue : value;
        }
        return values.get(index);
    }
    @Override public int size() { return values.size(); }
    @Override public T set(int index, T element) {
        range(index);
        T value = Objects.requireNonNullElse(element, defaultValue);
        T old = get(index);
        values.set(index, value);
        if (setter != null) setter.accept(index, value);
        return old;
    }
    @Override public void add(int index, T element) {
        if (loader != null) throw new UnsupportedOperationException("mapped list has fixed size");
        if (index < 0 || index > values.size()) throw new IndexOutOfBoundsException(index);
        values.add(index, Objects.requireNonNullElse(element, defaultValue));
    }
    @Override public T remove(int index) {
        range(index);
        if (loader != null) {
            T old = get(index);
            if (setter != null) setter.accept(index, defaultValue);
            return old;
        }
        return values.remove(index);
    }
    private void range(int index) { if (index < 0 || index >= values.size()) throw new IndexOutOfBoundsException(index); }
}
