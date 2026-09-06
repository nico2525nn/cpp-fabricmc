package net.minecraft.world.chunk;

/** Callback ABI used when a palette needs a larger storage width. */
public interface PaletteResizeListener<T> {
    int onResize(int newBits, T object);
}
