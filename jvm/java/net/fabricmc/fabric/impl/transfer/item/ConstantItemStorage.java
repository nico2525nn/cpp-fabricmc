package net.fabricmc.fabric.impl.transfer.item;

import net.fabricmc.fabric.api.transfer.v1.item.ItemVariant;
import net.fabricmc.fabric.api.transfer.v1.storage.base.SingleSlotStorage;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;

public final class ConstantItemStorage implements SingleSlotStorage<ItemVariant> {
    private final ItemVariant resource;
    private final long amount;
    public ConstantItemStorage(ItemVariant resource, long amount) { this.resource = resource; this.amount = Math.max(0, amount); }
    @Override public long insert(ItemVariant resource, long max, TransactionContext tx) { return 0; }
    @Override public long extract(ItemVariant resource, long max, TransactionContext tx) { return 0; }
    @Override public boolean isResourceBlank() { return resource == null || resource.isBlank() || amount == 0; }
    @Override public ItemVariant getResource() { return resource == null ? ItemVariant.blank() : resource; }
    @Override public long getAmount() { return amount; }
    @Override public long getCapacity() { return amount; }
}
