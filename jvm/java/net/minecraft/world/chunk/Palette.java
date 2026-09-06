package net.minecraft.world.chunk;

import java.util.List;
import net.minecraft.util.collection.IdList;

/** Palette factory ABI shared by PalettedContainer providers. */
public interface Palette<T> {
    T get(int index);
    int getIndex(T value);

    interface Factory<T> {
        Palette<T> create(int bits, IdList<T> idList, Object resizeListener, List<T> entries);
    }
}
