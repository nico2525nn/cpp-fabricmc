package net.fabricmc.fabric.api.transfer.v1.storage;

import java.util.AbstractList;
import java.util.List;
import net.fabricmc.fabric.api.transfer.v1.storage.base.SingleSlotStorage;

/** Storage made of indexed slots. */
public interface SlottedStorage<T> extends Storage<T> {
    int getSlotCount();
    SingleSlotStorage<T> getSlot(int slot);
    default List<SingleSlotStorage<T>> getSlots() {
        return new AbstractList<>() {
            @Override public SingleSlotStorage<T> get(int index) { return getSlot(index); }
            @Override public int size() { return getSlotCount(); }
        };
    }
}
