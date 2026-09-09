package net.fabricmc.fabric.api.transfer.v1.storage.base;

import java.util.Iterator;
import java.util.List;
import java.util.NoSuchElementException;
import net.fabricmc.fabric.api.transfer.v1.storage.Storage;
import net.fabricmc.fabric.api.transfer.v1.storage.StoragePreconditions;
import net.fabricmc.fabric.api.transfer.v1.storage.StorageView;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;

/** Ordered composition of multiple storages. */
public class CombinedStorage<T, S extends Storage<T>> implements Storage<T> {
    public List<S> parts;
    public CombinedStorage(List<S> parts) { this.parts = parts == null ? List.of() : parts; }
    @Override public boolean supportsInsertion() { return parts.stream().anyMatch(Storage::supportsInsertion); }
    @Override public long insert(T resource, long maxAmount, TransactionContext tx) {
        StoragePreconditions.notNegative(maxAmount);
        long total = 0;
        for (S part : parts) { total += part.insert(resource, maxAmount - total, tx); if (total >= maxAmount) break; }
        return total;
    }
    @Override public boolean supportsExtraction() { return parts.stream().anyMatch(Storage::supportsExtraction); }
    @Override public long extract(T resource, long maxAmount, TransactionContext tx) {
        StoragePreconditions.notNegative(maxAmount);
        long total = 0;
        for (S part : parts) { total += part.extract(resource, maxAmount - total, tx); if (total >= maxAmount) break; }
        return total;
    }
    @Override public Iterator<StorageView<T>> iterator() {
        return new CombinedIterator();
    }

    /** Iterator kept as a named nested type to match Fabric's public ABI. */
    final class CombinedIterator implements Iterator<StorageView<T>> {
        private final Iterator<S> partIterator = parts.iterator();
        private Iterator<? extends StorageView<T>> currentPartIterator = java.util.Collections.emptyIterator();

        private void advanceCurrentPartIterator() {
            while (!currentPartIterator.hasNext() && partIterator.hasNext()) {
                currentPartIterator = partIterator.next().iterator();
            }
        }

        @Override public boolean hasNext() {
            advanceCurrentPartIterator();
            return currentPartIterator.hasNext();
        }

        @Override public StorageView<T> next() {
            if (!hasNext()) throw new NoSuchElementException();
            return currentPartIterator.next();
        }
    }
    @Override public String toString() { return "CombinedStorage[" + parts + "]"; }
}
