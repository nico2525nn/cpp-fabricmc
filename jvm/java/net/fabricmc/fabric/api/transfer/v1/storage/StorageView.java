package net.fabricmc.fabric.api.transfer.v1.storage;

import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;

/** A view of one resource entry in a transfer storage. */
public interface StorageView<T> {
    long extract(T resource, long maxAmount, TransactionContext transaction);
    boolean isResourceBlank();
    T getResource();
    long getAmount();
    long getCapacity();
    default StorageView<T> getUnderlyingView() { return this; }
}
