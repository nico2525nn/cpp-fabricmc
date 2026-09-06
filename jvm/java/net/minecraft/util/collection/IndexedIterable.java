package net.minecraft.util.collection;

/** Indexed registry view used by palette and entity bootstrap code. */
public class IndexedIterable<T> extends IdList<T> {
    public static final int ABSENT_RAW_ID = -1;
    public IndexedIterable() { super(); }
    public IndexedIterable(int size) { super(size); }
    public T getOrThrow(int index) {
        T value = get(index);
        if (value == null) throw new IllegalArgumentException("missing indexed value " + index);
        return value;
    }
    public int getRawIdOrThrow(T value) {
        int id = getRawId(value);
        if (id < 0) throw new IllegalArgumentException("value is not indexed");
        return id;
    }
}
