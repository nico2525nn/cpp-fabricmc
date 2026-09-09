package net.fabricmc.fabric.api.transfer.v1.context;

import java.util.List;
import net.fabricmc.fabric.api.lookup.v1.item.ItemApiLookup;
import net.fabricmc.fabric.api.transfer.v1.item.ItemVariant;
import net.fabricmc.fabric.api.transfer.v1.storage.base.SingleSlotStorage;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.item.ItemStack;
import net.minecraft.screen.ScreenHandler;
import net.minecraft.util.Hand;

/** Context for transfer APIs exposed by a container item. */
public interface ContainerItemContext {
    static ContainerItemContext forPlayerInteraction(PlayerEntity player, Hand hand) {
        return ofPlayerHand(player, hand);
    }
    static ContainerItemContext forCreativeInteraction(PlayerEntity player, ItemStack stack) {
        return withConstant(stack);
    }
    static ContainerItemContext ofPlayerHand(PlayerEntity player, Hand hand) {
        return ofSingleSlot(net.fabricmc.fabric.api.transfer.v1.item.PlayerInventoryStorage.of(player).getHandSlot(hand));
    }
    static ContainerItemContext ofPlayerCursor(PlayerEntity player, ScreenHandler handler) {
        return ofSingleSlot(net.fabricmc.fabric.api.transfer.v1.item.PlayerInventoryStorage.getCursorStorage(handler));
    }
    static ContainerItemContext ofPlayerSlot(PlayerEntity player, SingleSlotStorage<ItemVariant> slot) { return ofSingleSlot(slot); }
    static ContainerItemContext ofSingleSlot(SingleSlotStorage<ItemVariant> slot) {
        return new net.fabricmc.fabric.impl.transfer.item.ContainerItemContextImpl(slot, List.of());
    }
    static ContainerItemContext withConstant(ItemStack stack) {
        return withConstant(ItemVariant.of(stack), stack == null ? 0 : stack.getCount());
    }
    static ContainerItemContext withConstant(ItemVariant variant, long amount) {
        return new net.fabricmc.fabric.impl.transfer.item.ContainerItemContextImpl(
            new net.fabricmc.fabric.impl.transfer.item.ConstantItemStorage(variant, amount), List.of());
    }
    default <A> A find(ItemApiLookup<A, ContainerItemContext> lookup) { return lookup == null ? null : lookup.find(this.getItemVariant().toStack(), this); }
    ItemVariant getItemVariant();
    long getAmount();
    default long insert(ItemVariant resource, long max, TransactionContext tx) { return getMainSlot().insert(resource, max, tx); }
    default long extract(ItemVariant resource, long max, TransactionContext tx) { return getMainSlot().extract(resource, max, tx); }
    default long exchange(ItemVariant resource, long max, TransactionContext tx) {
        long old = getAmount();
        if (old > 0) extract(getItemVariant(), old, tx);
        long inserted = insert(resource, max, tx);
        if (inserted < max && old > 0) insert(getItemVariant(), old, tx);
        return inserted;
    }
    SingleSlotStorage<ItemVariant> getMainSlot();
    default long insertOverflow(ItemVariant resource, long max, TransactionContext tx) {
        long inserted = insert(resource, max, tx);
        for (SingleSlotStorage<ItemVariant> slot : getAdditionalSlots()) {
            if (inserted >= max) break;
            inserted += slot.insert(resource, max - inserted, tx);
        }
        return inserted;
    }
    List<SingleSlotStorage<ItemVariant>> getAdditionalSlots();
}
