package net.fabricmc.fabric.api.transfer.v1.storage;

import java.util.Iterator;
import java.util.concurrent.atomic.AtomicLong;
import net.fabricmc.fabric.api.transfer.v1.transaction.Transaction;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;

/** Generic transactional storage contract. */
public interface Storage<T> extends Iterable<StorageView<T>> {
    Storage<?> EMPTY = new Storage<Object>() {
        @Override public long insert(Object resource, long max, TransactionContext tx) { return 0; }
        @Override public long extract(Object resource, long max, TransactionContext tx) { return 0; }
        @Override public Iterator<StorageView<Object>> iterator() { return java.util.Collections.emptyIterator(); }
        @Override public boolean supportsInsertion() { return false; }
        @Override public boolean supportsExtraction() { return false; }
    };
    AtomicLong VERSION = new AtomicLong();

    @SuppressWarnings("unchecked")
    static <T> Storage<T> empty() { return (Storage<T>) EMPTY; }
    default boolean supportsInsertion() { return true; }
    long insert(T resource, long maxAmount, TransactionContext transaction);
    default boolean supportsExtraction() { return true; }
    long extract(T resource, long maxAmount, TransactionContext transaction);
    @Override Iterator<StorageView<T>> iterator();

    default Iterator<StorageView<T>> nonEmptyIterator() {
        Iterator<StorageView<T>> source = iterator();
        return new Iterator<>() {
            private StorageView<T> next;
            private boolean ready;
            private void prepare() {
                if (ready) return;
                while (source.hasNext()) {
                    StorageView<T> candidate = source.next();
                    if (candidate != null && candidate.getAmount() > 0 && !candidate.isResourceBlank()) {
                        next = candidate; ready = true; return;
                    }
                }
                next = null; ready = true;
            }
            @Override public boolean hasNext() { prepare(); return next != null; }
            @Override public StorageView<T> next() {
                prepare();
                if (next == null) throw new java.util.NoSuchElementException();
                StorageView<T> result = next; ready = false; next = null; return result;
            }
            @Override public void remove() { throw new UnsupportedOperationException(); }
        };
    }
    default Iterable<StorageView<T>> nonEmptyViews() { return this::nonEmptyIterator; }
    default long getVersion() {
        if (Transaction.isOpen()) throw new IllegalStateException("getVersion() may not be called during a transaction");
        return VERSION.incrementAndGet();
    }
    @SuppressWarnings("unchecked") static <T> Class<Storage<T>> asClass() { return (Class<Storage<T>>) (Object) Storage.class; }
}
