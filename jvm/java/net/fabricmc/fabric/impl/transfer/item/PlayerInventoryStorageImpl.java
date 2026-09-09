package net.fabricmc.fabric.impl.transfer.item;

import net.fabricmc.fabric.api.transfer.v1.item.ItemVariant;
import net.fabricmc.fabric.api.transfer.v1.item.PlayerInventoryStorage;
import net.fabricmc.fabric.api.transfer.v1.storage.StorageUtil;
import net.fabricmc.fabric.api.transfer.v1.storage.base.SingleSlotStorage;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.entity.player.PlayerInventory;
import net.minecraft.util.Hand;

public final class PlayerInventoryStorageImpl extends InventoryStorageImpl implements PlayerInventoryStorage {
    private final PlayerEntity player;
    public PlayerInventoryStorageImpl(PlayerEntity player) {
        super(player.getInventory(), null); this.player = player;
    }
    public PlayerInventoryStorageImpl(PlayerInventory inventory, PlayerEntity player) {
        super(inventory, null); this.player = player;
    }
    @Override public long insert(ItemVariant resource, long max, TransactionContext tx) {
        return StorageUtil.tryInsertStacking(this, resource, max, tx);
    }
    @Override public long offer(ItemVariant resource, long max, TransactionContext tx) {
        return StorageUtil.tryInsertStacking(this, resource, max, tx);
    }
    @Override public void drop(ItemVariant resource, long max, boolean randomly, boolean retain, TransactionContext tx) {
        if (player == null || resource == null || max <= 0) return;
        long removed = extract(resource, max, tx);
        if (removed > 0) player.dropItem(resource.toStack((int) Math.min(Integer.MAX_VALUE, removed)), randomly, retain);
    }
    @Override public SingleSlotStorage<ItemVariant> getHandSlot(Hand hand) {
        int index = hand == Hand.OFF_HAND ? 40 : 27 + Math.max(0, Math.min(8, inventory instanceof PlayerInventory p ? p.selectedSlot : 0));
        return new SlotStorage(index);
    }
}
