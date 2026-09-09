package net.fabricmc.fabric.api.transfer.v1.storage.base;

import java.util.List;
import net.fabricmc.fabric.api.transfer.v1.storage.SlottedStorage;

/** Composition of multiple slotted storages with a flattened slot index. */
public class CombinedSlottedStorage<T, S extends SlottedStorage<T>>
        extends CombinedStorage<T, S> implements SlottedStorage<T> {
    public CombinedSlottedStorage(List<S> parts) { super(parts); }
    @Override public int getSlotCount() { return parts.stream().mapToInt(SlottedStorage::getSlotCount).sum(); }
    @Override public SingleSlotStorage<T> getSlot(int slot) {
        if (slot < 0) throw new IndexOutOfBoundsException("slot " + slot);
        int remaining = slot;
        for (S part : parts) {
            if (remaining < part.getSlotCount()) return part.getSlot(remaining);
            remaining -= part.getSlotCount();
        }
        throw new IndexOutOfBoundsException("slot " + slot + " / " + getSlotCount());
    }
    @Override public String toString() { return "CombinedSlottedStorage[" + parts + "]"; }
}
