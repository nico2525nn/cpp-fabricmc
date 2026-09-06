package net.minecraft.world.chunk.light;

import java.util.function.BiConsumer;

public interface LightSourceView {
    default Object getChunkSkyLight() { return null; }
    default void forEachLightSource(BiConsumer<?, ?> callback) {}
}
