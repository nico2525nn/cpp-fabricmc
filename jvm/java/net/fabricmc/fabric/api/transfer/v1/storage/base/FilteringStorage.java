package net.fabricmc.fabric.api.transfer.v1.storage.base;

import java.util.Iterator;
import java.util.function.Supplier;
import net.fabricmc.fabric.api.transfer.v1.storage.Storage;
import net.fabricmc.fabric.api.transfer.v1.storage.StorageView;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;

/** Delegating storage with independent insertion/extraction filters. */
public abstract class FilteringStorage<T> implements Storage<T> {
    protected final Supplier<Storage<T>> backingStorage;
    public static <T> Storage<T> insertOnlyOf(Storage<T> storage) { return of(storage, true, false); }
    public static <T> Storage<T> extractOnlyOf(Storage<T> storage) { return of(storage, false, true); }
    public static <T> Storage<T> readOnlyOf(Storage<T> storage) { return of(storage, false, false); }
    public static <T> Storage<T> of(Storage<T> storage, boolean allowInsert, boolean allowExtract) {
        if (allowInsert && allowExtract) return storage;
        return new FilteringStorage<T>(storage) {
            @Override protected boolean canInsert(T resource) { return allowInsert; }
            @Override protected boolean canExtract(T resource) { return allowExtract; }
            @Override public boolean supportsInsertion() { return allowInsert && super.supportsInsertion(); }
            @Override public boolean supportsExtraction() { return allowExtract && super.supportsExtraction(); }
        };
    }
    public FilteringStorage(Storage<T> storage) { this(() -> storage); }
    public FilteringStorage(Supplier<Storage<T>> storage) { this.backingStorage = storage; }
    protected boolean canInsert(T resource) { return true; }
    protected boolean canExtract(T resource) { return true; }
    @Override public boolean supportsInsertion() { return backingStorage.get().supportsInsertion(); }
    @Override public long insert(T resource, long max, TransactionContext tx) { return canInsert(resource) ? backingStorage.get().insert(resource, max, tx) : 0; }
    @Override public boolean supportsExtraction() { return backingStorage.get().supportsExtraction(); }
    @Override public long extract(T resource, long max, TransactionContext tx) { return canExtract(resource) ? backingStorage.get().extract(resource, max, tx) : 0; }
    @Override public Iterator<StorageView<T>> iterator() {
        Iterator<StorageView<T>> source = backingStorage.get().iterator();
        return new Iterator<>() {
            @Override public boolean hasNext() { return source.hasNext(); }
            @Override public StorageView<T> next() { return new FilteringStorageView(source.next()); }
            @Override public void remove() { throw new UnsupportedOperationException(); }
        };
    }
    @Override public long getVersion() { return backingStorage.get().getVersion(); }
    @Override public String toString() { return "FilteringStorage[" + backingStorage.get() + "]"; }
    private final class FilteringStorageView implements StorageView<T> {
        private final StorageView<T> backing;
        private FilteringStorageView(StorageView<T> backing) { this.backing = backing; }
        @Override public long extract(T resource, long max, TransactionContext tx) { return canExtract(resource) ? backing.extract(resource, max, tx) : 0; }
        @Override public boolean isResourceBlank() { return backing.isResourceBlank(); }
        @Override public T getResource() { return backing.getResource(); }
        @Override public long getAmount() { return backing.getAmount(); }
        @Override public long getCapacity() { return backing.getCapacity(); }
        @Override public StorageView<T> getUnderlyingView() { return backing.getUnderlyingView(); }
    }
}
