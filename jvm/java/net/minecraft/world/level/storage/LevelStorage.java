package net.minecraft.world.level.storage;

/** Level-storage namespace retained for constructor/link compatibility. */
public final class LevelStorage {
    private LevelStorage() { }
    public static class Session implements AutoCloseable {
        public Session() { }
        @Override public void close() { }
    }
}
