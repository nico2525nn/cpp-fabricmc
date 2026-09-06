package net.minecraft.util.collection;

/** Integer id lookup surface used by paletted chunk containers. */
public class IdList<T> {
    private final int size;
    public IdList() { this(0); }
    public IdList(int size) { this.size = Math.max(0, size); }
    public int size() { return size; }
    public int getRawId(T value) { return value == null ? -1 : 0; }
    public T get(int id) { return null; }
}
