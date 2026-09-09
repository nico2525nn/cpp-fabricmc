package net.fabricmc.fabric.api.transfer.v1.storage;

import java.util.List;
import java.util.Objects;
import java.util.function.Predicate;
import net.fabricmc.fabric.api.transfer.v1.storage.base.ResourceAmount;
import net.fabricmc.fabric.api.transfer.v1.storage.base.SingleSlotStorage;
import net.fabricmc.fabric.api.transfer.v1.transaction.Transaction;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;

/** Correctness-first helpers for composing transactional storages. */
public final class StorageUtil {
    private StorageUtil() { }

    public static <T> long move(Storage<T> from, Storage<T> to, Predicate<T> filter,
                                long maxAmount, TransactionContext transaction) {
        Objects.requireNonNull(filter, "filter");
        StoragePreconditions.notNegative(maxAmount);
        if (from == null || to == null || maxAmount == 0) return 0;
        long moved = 0;
        try (Transaction outer = Transaction.openNested(transaction)) {
            for (StorageView<T> view : from.nonEmptyViews()) {
                if (moved >= maxAmount) break;
                T resource = view.getResource();
                if (!filter.test(resource)) continue;
                long extractable = simulateExtract(view, resource, maxAmount - moved, outer);
                if (extractable <= 0) continue;
                try (Transaction transfer = outer.openNested()) {
                    long inserted = to.insert(resource, extractable, transfer);
                    long extracted = view.extract(resource, inserted, transfer);
                    if (extracted == inserted) {
                        moved += inserted;
                        transfer.commit();
                    }
                }
            }
            outer.commit();
        }
        return moved;
    }

    public static <T> long simulateInsert(Storage<T> storage, T resource, long maxAmount,
                                          TransactionContext transaction) {
        try (Transaction nested = Transaction.openNested(transaction)) {
            long result = storage == null ? 0 : storage.insert(resource, maxAmount, nested);
            return result;
        }
    }

    public static <T> long simulateExtract(Storage<T> storage, T resource, long maxAmount,
                                           TransactionContext transaction) {
        try (Transaction nested = Transaction.openNested(transaction)) {
            long result = storage == null ? 0 : storage.extract(resource, maxAmount, nested);
            return result;
        }
    }

    public static <T> long simulateExtract(StorageView<T> view, T resource, long maxAmount,
                                           TransactionContext transaction) {
        try (Transaction nested = Transaction.openNested(transaction)) {
            long result = view == null ? 0 : view.extract(resource, maxAmount, nested);
            return result;
        }
    }

    public static <T, S extends Object & Storage<T> & StorageView<T>> long simulateExtract(
            S storage, T resource, long maxAmount, TransactionContext transaction) {
        try (Transaction nested = Transaction.openNested(transaction)) {
            return storage.extract(resource, maxAmount, nested);
        }
    }

    public static <T> ResourceAmount<T> extractAny(Storage<T> storage, long maxAmount,
                                                   TransactionContext transaction) {
        StoragePreconditions.notNegative(maxAmount);
        if (storage == null) return null;
        for (StorageView<T> view : storage.nonEmptyViews()) {
            T resource = view.getResource();
            long extracted = view.extract(resource, maxAmount, transaction);
            if (extracted > 0) return new ResourceAmount<>(resource, extracted);
        }
        return null;
    }

    public static <T> long insertStacking(List<? extends SingleSlotStorage<T>> slots,
                                          T resource, long maxAmount,
                                          TransactionContext transaction) {
        StoragePreconditions.notNegative(maxAmount);
        if (slots == null) return 0;
        long inserted = 0;
        for (SingleSlotStorage<T> slot : slots) {
            if (slot != null && !slot.isResourceBlank()) {
                inserted += slot.insert(resource, maxAmount - inserted, transaction);
                if (inserted >= maxAmount) return inserted;
            }
        }
        for (SingleSlotStorage<T> slot : slots) {
            if (slot != null) {
                inserted += slot.insert(resource, maxAmount - inserted, transaction);
                if (inserted >= maxAmount) return inserted;
            }
        }
        return inserted;
    }

    public static <T> long tryInsertStacking(Storage<T> storage, T resource, long maxAmount,
                                             TransactionContext transaction) {
        StoragePreconditions.notNegative(maxAmount);
        if (storage == null) return 0;
        return storage instanceof SlottedStorage<T> slotted
            ? insertStacking(slotted.getSlots(), resource, maxAmount, transaction)
            : storage.insert(resource, maxAmount, transaction);
    }

    public static <T> T findStoredResource(Storage<T> storage) {
        return findStoredResource(storage, ignored -> true);
    }

    public static <T> T findStoredResource(Storage<T> storage, Predicate<T> filter) {
        Objects.requireNonNull(filter, "filter");
        if (storage == null) return null;
        for (StorageView<T> view : storage.nonEmptyViews())
            if (filter.test(view.getResource())) return view.getResource();
        return null;
    }

    public static <T> T findExtractableResource(Storage<T> storage, TransactionContext transaction) {
        return findExtractableResource(storage, ignored -> true, transaction);
    }

    public static <T> T findExtractableResource(Storage<T> storage, Predicate<T> filter,
                                                TransactionContext transaction) {
        Objects.requireNonNull(filter, "filter");
        if (storage == null) return null;
        try (Transaction nested = Transaction.openNested(transaction)) {
            for (StorageView<T> view : storage.nonEmptyViews()) {
                T resource = view.getResource();
                if (filter.test(resource) && view.extract(resource, Long.MAX_VALUE, nested) > 0)
                    return resource;
            }
        }
        return null;
    }

    public static <T> ResourceAmount<T> findExtractableContent(Storage<T> storage,
                                                                TransactionContext transaction) {
        return findExtractableContent(storage, ignored -> true, transaction);
    }

    public static <T> ResourceAmount<T> findExtractableContent(Storage<T> storage,
            Predicate<T> filter, TransactionContext transaction) {
        Objects.requireNonNull(filter, "filter");
        if (storage == null) return null;
        try (Transaction nested = Transaction.openNested(transaction)) {
            for (StorageView<T> view : storage.nonEmptyViews()) {
                T resource = view.getResource();
                if (filter.test(resource)) {
                    long amount = view.extract(resource, Long.MAX_VALUE, nested);
                    if (amount > 0) return new ResourceAmount<>(resource, amount);
                }
            }
        }
        return null;
    }

    public static <T> int calculateComparatorOutput(Storage<T> storage) {
        if (storage == null) return 0;
        long amount = 0;
        long capacity = 0;
        for (StorageView<T> view : storage) {
            if (view == null) continue;
            amount += Math.max(0, view.getAmount());
            capacity += Math.max(0, view.getCapacity());
        }
        if (capacity <= 0 || amount <= 0) return 0;
        return Math.min(15, (int) Math.ceil(amount * 14.0 / capacity) + 1);
    }
}
