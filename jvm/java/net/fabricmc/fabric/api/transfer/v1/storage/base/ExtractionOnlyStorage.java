package net.fabricmc.fabric.api.transfer.v1.storage.base;

import net.fabricmc.fabric.api.transfer.v1.storage.Storage;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;

public interface ExtractionOnlyStorage<T> extends Storage<T> {
    @Override default boolean supportsInsertion() { return false; }
    @Override default long insert(T resource, long maxAmount, TransactionContext transaction) { return 0; }
}
