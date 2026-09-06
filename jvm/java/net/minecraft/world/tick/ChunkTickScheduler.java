package net.minecraft.world.tick;

/** Generic tick scheduler handle retained for chunk constructors and accessors. */
public class ChunkTickScheduler<T> {
    public ChunkTickScheduler() { }
    public void schedule(T value) { }
    public int getTickCount() { return 0; }
}
