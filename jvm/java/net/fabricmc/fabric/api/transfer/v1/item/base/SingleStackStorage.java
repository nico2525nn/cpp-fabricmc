package net.fabricmc.fabric.api.transfer.v1.item.base;

import net.fabricmc.fabric.api.transfer.v1.item.ItemVariant;
import net.fabricmc.fabric.api.transfer.v1.storage.StoragePreconditions;
import net.fabricmc.fabric.api.transfer.v1.storage.base.SingleSlotStorage;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;
import net.fabricmc.fabric.api.transfer.v1.transaction.base.SnapshotParticipant;
import net.minecraft.item.ItemStack;

/** A transactional single-slot storage backed by an ItemStack. */
public abstract class SingleStackStorage extends SnapshotParticipant<ItemStack>
        implements SingleSlotStorage<ItemVariant> {
    protected abstract ItemStack getStack();
    protected abstract void setStack(ItemStack stack);
    protected boolean canInsert(ItemVariant variant) { return true; }
    protected boolean canExtract(ItemVariant variant) { return true; }
    protected int getCapacity(ItemVariant variant) { return variant.getItem().getMaxCount(); }

    @Override public boolean isResourceBlank() { return getStack() == null || getStack().isEmpty(); }
    @Override public ItemVariant getResource() { return ItemVariant.of(getStack()); }
    @Override public long getAmount() { return getStack() == null ? 0 : getStack().getCount(); }
    @Override public long getCapacity() { return getCapacity(getResource()); }

    @Override public long insert(ItemVariant inserted, long max, TransactionContext tx) {
        StoragePreconditions.notBlankNotNegative(inserted, max);
        ItemStack current = getStack();
        if (!(current == null || current.isEmpty() || inserted.matches(current)) || !canInsert(inserted)) return 0;
        int accepted = (int) Math.min(max, Math.max(0, getCapacity(inserted) - (current == null ? 0 : current.getCount())));
        if (accepted <= 0) return 0;
        updateSnapshots(tx);
        current = getStack();
        if (current == null || current.isEmpty()) setStack(inserted.toStack(accepted));
        else { ItemStack copy = current.copy(); copy.increment(accepted); setStack(copy); }
        return accepted;
    }

    @Override public long extract(ItemVariant extracted, long max, TransactionContext tx) {
        StoragePreconditions.notBlankNotNegative(extracted, max);
        ItemStack current = getStack();
        if (current == null || !extracted.matches(current) || !canExtract(extracted)) return 0;
        int removed = (int) Math.min(max, current.getCount());
        if (removed <= 0) return 0;
        updateSnapshots(tx);
        current = getStack();
        ItemStack copy = current.copy();
        copy.decrement(removed);
        setStack(copy.isEmpty() ? ItemStack.EMPTY : copy);
        return removed;
    }

    @Override protected ItemStack createSnapshot() {
        ItemStack current = getStack();
        return current == null ? ItemStack.EMPTY : current.copy();
    }
    @Override protected void readSnapshot(ItemStack snapshot) { setStack(snapshot == null ? ItemStack.EMPTY : snapshot.copy()); }
    @Override public String toString() { return "SingleStackStorage[" + getStack() + "]"; }
}
