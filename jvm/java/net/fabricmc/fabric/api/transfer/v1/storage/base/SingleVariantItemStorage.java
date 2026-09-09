package net.fabricmc.fabric.api.transfer.v1.storage.base;

import net.fabricmc.fabric.api.transfer.v1.context.ContainerItemContext;
import net.fabricmc.fabric.api.transfer.v1.item.ItemVariant;
import net.fabricmc.fabric.api.transfer.v1.storage.StoragePreconditions;
import net.fabricmc.fabric.api.transfer.v1.storage.TransferVariant;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;
import net.minecraft.item.Item;

/** Fixed-capacity storage encoded in an item's component changes. */
public abstract class SingleVariantItemStorage<T extends TransferVariant<?>>
        implements SingleSlotStorage<T> {
    private final ContainerItemContext context;
    private final Item item;
    public SingleVariantItemStorage(ContainerItemContext context) {
        this.context = java.util.Objects.requireNonNull(context, "context");
        this.item = context.getItemVariant().getItem();
    }
    protected abstract T getBlankResource();
    protected abstract T getResource(ItemVariant currentVariant);
    protected abstract long getAmount(ItemVariant currentVariant);
    protected abstract long getCapacity(T resource);
    protected abstract ItemVariant getUpdatedVariant(ItemVariant currentVariant, T resource, long amount);
    protected boolean canInsert(T resource) { return true; }
    protected boolean canExtract(T resource) { return true; }
    private boolean update(T resource, long amount, TransactionContext tx) {
        return context.exchange(getUpdatedVariant(context.getItemVariant(), resource, amount), 1, tx) == 1;
    }
    @Override public boolean supportsInsertion() { return context.getItemVariant().isOf(item); }
    @Override public long insert(T resource, long max, TransactionContext tx) {
        StoragePreconditions.notBlankNotNegative(resource, max);
        if (!supportsInsertion() || !canInsert(resource)) return 0;
        T current = getResource(context.getItemVariant());
        long amount = getAmount(context.getItemVariant());
        long accepted = current.isBlank() || amount == 0 ? Math.min(max, getCapacity(resource))
            : current.equals(resource) ? Math.min(max, Math.max(0, getCapacity(resource) - amount)) : 0;
        return accepted > 0 && update(resource, amount + accepted, tx) ? accepted : 0;
    }
    @Override public boolean supportsExtraction() { return context.getItemVariant().isOf(item); }
    @Override public long extract(T resource, long max, TransactionContext tx) {
        StoragePreconditions.notBlankNotNegative(resource, max);
        if (!supportsExtraction() || !canExtract(resource)) return 0;
        T current = getResource(context.getItemVariant());
        long amount = getAmount(context.getItemVariant());
        long removed = current.equals(resource) ? Math.min(max, amount) : 0;
        return removed > 0 && update(resource, amount - removed, tx) ? removed : 0;
    }
    @Override public boolean isResourceBlank() { return getResource().isBlank(); }
    @Override public T getResource() { return context.getItemVariant().isOf(item) ? getResource(context.getItemVariant()) : getBlankResource(); }
    @Override public long getAmount() { return context.getItemVariant().isOf(item) ? getAmount(context.getItemVariant()) : 0; }
    @Override public long getCapacity() { return context.getItemVariant().isOf(item) ? getCapacity(getResource()) : 0; }
    @Override public String toString() { return "SingleVariantItemStorage[" + context + "/" + item + "]"; }
}
