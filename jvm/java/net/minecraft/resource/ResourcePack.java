package net.minecraft.resource;

/** Closeable resource-pack handle used by the shadow resource API. */
public interface ResourcePack extends AutoCloseable {
    @Override default void close() {}
}
