package net.fabricmc.fabric.api.transfer.v1.storage.base;

import java.util.Collections;
import java.util.Iterator;
import net.fabricmc.fabric.api.transfer.v1.storage.Storage;
import net.fabricmc.fabric.api.transfer.v1.storage.StorageView;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;

public interface InsertionOnlyStorage<T> extends Storage<T> {
    @Override default boolean supportsExtraction() { return false; }
    @Override default long extract(T resource, long maxAmount, TransactionContext transaction) { return 0; }
    @Override default Iterator<StorageView<T>> iterator() { return Collections.emptyIterator(); }
}
