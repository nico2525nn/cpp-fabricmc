package net.minecraft.entity.data;

/** Identity token for a tracked entity value. */
public final class TrackedData<T> {
    private final int id;

    public TrackedData() { this(0); }
    public TrackedData(int id) { this.id = id; }
    public int getId() { return id; }
}
