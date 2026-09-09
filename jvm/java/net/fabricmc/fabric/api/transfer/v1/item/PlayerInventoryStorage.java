package net.fabricmc.fabric.api.transfer.v1.item;

import net.fabricmc.fabric.api.transfer.v1.storage.base.SingleSlotStorage;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.entity.player.PlayerInventory;
import net.minecraft.screen.ScreenHandler;
import net.minecraft.util.Hand;

/** Transfer view of player inventory, hands and cursor. */
public interface PlayerInventoryStorage extends InventoryStorage {
    static PlayerInventoryStorage of(PlayerEntity player) {
        return new net.fabricmc.fabric.impl.transfer.item.PlayerInventoryStorageImpl(
            java.util.Objects.requireNonNull(player, "player"));
    }
    static PlayerInventoryStorage of(PlayerInventory inventory) {
        return new net.fabricmc.fabric.impl.transfer.item.PlayerInventoryStorageImpl(inventory, null);
    }
    static SingleSlotStorage<ItemVariant> getCursorStorage(ScreenHandler handler) {
        return new net.fabricmc.fabric.impl.transfer.item.CursorStorage(handler);
    }
    long insert(ItemVariant resource, long maxAmount, TransactionContext transaction);
    default void offerOrDrop(ItemVariant resource, long maxAmount, TransactionContext transaction) {
        long inserted = offer(resource, maxAmount, transaction);
        if (inserted < maxAmount) drop(resource, maxAmount - inserted, false, false, transaction);
    }
    long offer(ItemVariant resource, long maxAmount, TransactionContext transaction);
    void drop(ItemVariant resource, long maxAmount, boolean randomly, boolean retainOwnership, TransactionContext transaction);
    default void drop(ItemVariant resource, long maxAmount, boolean randomly, TransactionContext transaction) {
        drop(resource, maxAmount, randomly, false, transaction);
    }
    default void drop(ItemVariant resource, long maxAmount, TransactionContext transaction) {
        drop(resource, maxAmount, false, false, transaction);
    }
    SingleSlotStorage<ItemVariant> getHandSlot(Hand hand);
}
