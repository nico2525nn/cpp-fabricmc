package net.fabricmc.fabric.impl.transfer.item;

import java.util.ArrayList;
import java.util.List;
import net.fabricmc.fabric.api.transfer.v1.item.InventoryStorage;
import net.fabricmc.fabric.api.transfer.v1.item.ItemVariant;
import net.fabricmc.fabric.api.transfer.v1.item.base.SingleStackStorage;
import net.minecraft.inventory.Inventory;
import net.minecraft.inventory.SidedInventory;
import net.minecraft.item.ItemStack;
import net.minecraft.util.math.Direction;

/** Inventory adapter shared by the public InventoryStorage and player view. */
public class InventoryStorageImpl implements InventoryStorage {
    protected final Inventory inventory;
    private final Direction side;
    private final List<net.fabricmc.fabric.api.transfer.v1.storage.base.SingleSlotStorage<ItemVariant>> slots;

    public InventoryStorageImpl(Inventory inventory, Direction side) {
        this.inventory = java.util.Objects.requireNonNull(inventory, "inventory");
        this.side = side;
        this.slots = createSlots();
    }

    private List<net.fabricmc.fabric.api.transfer.v1.storage.base.SingleSlotStorage<ItemVariant>> createSlots() {
        List<Integer> indices = new ArrayList<>();
        if (inventory instanceof SidedInventory sided && side != null) {
            for (int index : sided.getAvailableSlots(side)) indices.add(index);
        } else {
            for (int index = 0; index < inventory.size(); index++) indices.add(index);
        }
        List<net.fabricmc.fabric.api.transfer.v1.storage.base.SingleSlotStorage<ItemVariant>> result = new ArrayList<>();
        for (int index : indices) result.add(new SlotStorage(index));
        return List.copyOf(result);
    }

    @Override public List<net.fabricmc.fabric.api.transfer.v1.storage.base.SingleSlotStorage<ItemVariant>> getSlots() { return slots; }
    @Override public long insert(ItemVariant resource, long max, net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext transaction) {
        net.fabricmc.fabric.api.transfer.v1.storage.StoragePreconditions.notBlankNotNegative(resource, max);
        long total = 0;
        for (var slot : slots) {
            total += slot.insert(resource, max - total, transaction);
            if (total >= max) break;
        }
        return total;
    }
    @Override public long extract(ItemVariant resource, long max, net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext transaction) {
        net.fabricmc.fabric.api.transfer.v1.storage.StoragePreconditions.notBlankNotNegative(resource, max);
        long total = 0;
        for (var slot : slots) {
            total += slot.extract(resource, max - total, transaction);
            if (total >= max) break;
        }
        return total;
    }
    @Override public java.util.Iterator<net.fabricmc.fabric.api.transfer.v1.storage.StorageView<ItemVariant>> iterator() {
        return slots.stream().map(slot -> (net.fabricmc.fabric.api.transfer.v1.storage.StorageView<ItemVariant>) slot).iterator();
    }

    protected class SlotStorage extends SingleStackStorage {
        private final int slot;
        SlotStorage(int slot) { this.slot = slot; }
        @Override protected ItemStack getStack() { return inventory.getStack(slot); }
        @Override protected void setStack(ItemStack stack) { inventory.setStack(slot, stack == null ? ItemStack.EMPTY : stack); inventory.markDirty(); }
        @Override protected boolean canInsert(ItemVariant variant) {
            ItemStack stack = variant == null ? ItemStack.EMPTY : variant.toStack();
            return inventory.isValid(slot, stack)
                && (!(inventory instanceof SidedInventory sided) || side == null || sided.canInsert(slot, stack, side));
        }
        @Override protected boolean canExtract(ItemVariant variant) {
            ItemStack stack = getStack();
            return !(inventory instanceof SidedInventory sided) || side == null || sided.canExtract(slot, stack, side);
        }
    }
}
