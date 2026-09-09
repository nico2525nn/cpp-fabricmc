package net.fabricmc.fabric.api.transfer.v1.storage.base;

import java.util.Iterator;
import java.util.List;
import net.fabricmc.fabric.api.transfer.v1.storage.SlottedStorage;
import net.fabricmc.fabric.api.transfer.v1.storage.StorageView;

/** A one-slot storage that is also its own storage view. */
public interface SingleSlotStorage<T> extends SlottedStorage<T>, StorageView<T> {
    @Override default Iterator<StorageView<T>> iterator() { return List.<StorageView<T>>of(this).iterator(); }
    @Override default int getSlotCount() { return 1; }
    @Override default SingleSlotStorage<T> getSlot(int slot) {
        if (slot != 0) throw new IndexOutOfBoundsException("slot " + slot);
        return this;
    }
}
