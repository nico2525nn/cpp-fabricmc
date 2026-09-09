package net.fabricmc.fabric.api.transfer.v1.storage.base;

import net.fabricmc.fabric.api.transfer.v1.storage.StorageView;
import net.fabricmc.fabric.api.transfer.v1.storage.TransferVariant;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;

/** Empty view that advertises capacity. */
public class BlankVariantView<T extends TransferVariant<?>> implements StorageView<T> {
    private final T blankVariant;
    private final long capacity;
    public BlankVariantView(T blankVariant, long capacity) {
        if (blankVariant == null || !blankVariant.isBlank()) throw new IllegalArgumentException("Expected a blank variant");
        if (capacity < 0) throw new IllegalArgumentException("capacity");
        this.blankVariant = blankVariant; this.capacity = capacity;
    }
    @Override public long extract(T resource, long maxAmount, TransactionContext transaction) { return 0; }
    @Override public boolean isResourceBlank() { return true; }
    @Override public T getResource() { return blankVariant; }
    @Override public long getAmount() { return 0; }
    @Override public long getCapacity() { return capacity; }
}
