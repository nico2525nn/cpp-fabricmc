package net.minecraft.util.collection;

import java.util.AbstractSet;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Iterator;
import java.util.NoSuchElementException;
import java.util.SortedSet;

/** Array-backed sorted set used by Minecraft's tick and scheduler queues. */
public class SortedArraySet<T> extends AbstractSet<T> implements SortedSet<T> {
    public static final int DEFAULT_CAPACITY = 10;
    private Object[] elements;
    private int size;
    private final Comparator<? super T> comparator;

    private SortedArraySet(Comparator<? super T> comparator, int initialCapacity) {
        this.comparator = comparator;
        this.elements = new Object[Math.max(0, initialCapacity)];
    }

    public static <T extends Comparable<? super T>> SortedArraySet<T> create() {
        return create(null, DEFAULT_CAPACITY);
    }

    public static <T extends Comparable<? super T>> SortedArraySet<T> create(int initialCapacity) {
        return create(null, initialCapacity);
    }

    public static <T> SortedArraySet<T> create(Comparator<? super T> comparator) {
        return create(comparator, DEFAULT_CAPACITY);
    }

    public static <T> SortedArraySet<T> create(Comparator<? super T> comparator,
                                                int initialCapacity) {
        return new SortedArraySet<>(comparator, initialCapacity);
    }

    @SuppressWarnings("unchecked")
    private int compare(T left, T right) {
        if (comparator != null) return comparator.compare(left, right);
        return ((Comparable<? super T>) left).compareTo(right);
    }

    @SuppressWarnings("unchecked")
    private T element(int index) { return (T) elements[index]; }

    public int binarySearch(T object) {
        int low = 0, high = size - 1;
        while (low <= high) {
            int middle = (low + high) >>> 1;
            int comparison = compare(element(middle), object);
            if (comparison < 0) low = middle + 1;
            else if (comparison > 0) high = middle - 1;
            else return middle;
        }
        return -(low + 1);
    }

    public int insertionPoint(int binarySearchResult) {
        return binarySearchResult >= 0 ? binarySearchResult : -binarySearchResult - 1;
    }

    public void ensureCapacity(int minCapacity) {
        if (minCapacity > elements.length)
            elements = Arrays.copyOf(elements, Math.max(minCapacity, Math.max(1, elements.length * 2)));
    }

    public T addAndGet(T object) {
        int result = binarySearch(object);
        if (result >= 0) return element(result);
        int index = insertionPoint(result);
        ensureCapacity(size + 1);
        System.arraycopy(elements, index, elements, index + 1, size - index);
        elements[index] = object;
        ++size;
        return object;
    }

    @Override public boolean add(T object) {
        int result = binarySearch(object);
        if (result >= 0) return false;
        addAndGet(object);
        return true;
    }

    public void add(T object, int index) {
        if (index < 0 || index > size) throw new IndexOutOfBoundsException(index);
        ensureCapacity(size + 1);
        System.arraycopy(elements, index, elements, index + 1, size - index);
        elements[index] = object;
        ++size;
    }

    public T get(int index) {
        if (index < 0 || index >= size) throw new IndexOutOfBoundsException(index);
        return element(index);
    }

    public T getIfContains(T object) {
        int index = binarySearch(object);
        return index < 0 ? null : element(index);
    }

    public T first() { if (size == 0) throw new NoSuchElementException(); return element(0); }
    public T last() { if (size == 0) throw new NoSuchElementException(); return element(size - 1); }

    public T remove(int index) {
        T result = get(index);
        int moved = size - index - 1;
        if (moved > 0) System.arraycopy(elements, index + 1, elements, index, moved);
        elements[--size] = null;
        return result;
    }

    @Override public boolean remove(Object object) {
        @SuppressWarnings("unchecked") T value = (T) object;
        int index;
        try { index = binarySearch(value); } catch (ClassCastException ignored) { return false; }
        if (index < 0) return false;
        remove(index);
        return true;
    }

    @Override public boolean contains(Object object) {
        @SuppressWarnings("unchecked") T value = (T) object;
        try { return binarySearch(value) >= 0; } catch (ClassCastException ignored) { return false; }
    }

    @Override public int size() { return size; }

    @Override public void clear() {
        Arrays.fill(elements, 0, size, null);
        size = 0;
    }

    @Override public Iterator<T> iterator() {
        return new Iterator<>() {
            int cursor;
            int last = -1;
            @Override public boolean hasNext() { return cursor < size; }
            @Override public T next() {
                if (!hasNext()) throw new NoSuchElementException();
                last = cursor;
                return element(cursor++);
            }
            @Override public void remove() {
                if (last < 0) throw new IllegalStateException();
                SortedArraySet.this.remove(last);
                cursor = last;
                last = -1;
            }
        };
    }

    @Override public Comparator<? super T> comparator() { return comparator; }
    @Override public SortedSet<T> subSet(T fromElement, T toElement) { throw new UnsupportedOperationException(); }
    @Override public SortedSet<T> headSet(T toElement) { throw new UnsupportedOperationException(); }
    @Override public SortedSet<T> tailSet(T fromElement) { throw new UnsupportedOperationException(); }

    @Override public Object[] toArray() { return Arrays.copyOf(elements, size); }
    @Override public <E> E[] toArray(E[] array) {
        if (array.length < size) return cast(Arrays.copyOf(elements, size, array.getClass()));
        System.arraycopy(elements, 0, array, 0, size);
        if (array.length > size) array[size] = null;
        return array;
    }

    @SuppressWarnings("unchecked")
    public static <E> E[] cast(Object[] array) { return (E[]) array; }
}
