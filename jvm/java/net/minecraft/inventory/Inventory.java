package net.minecraft.inventory;

import java.util.Set;
import java.util.function.Predicate;
import net.minecraft.block.entity.BlockEntity;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;

/**
 * The small, server-side inventory contract used by Fabric mods.
 *
 * <p>The native server owns player storage; Java-only inventories use the
 * default implementations below. Keeping the common rules here is important
 * because mixins target this interface rather than a particular inventory
 * implementation.</p>
 */
public interface Inventory {
    float DEFAULT_MAX_INTERACTION_RANGE = 8.0F;

    int size();

    ItemStack getStack(int slot);

    default boolean isEmpty() {
        for (int slot = 0; slot < size(); slot++) {
            if (!getStack(slot).isEmpty()) return false;
        }
        return true;
    }

    default ItemStack removeStack(int slot, int amount) {
        if (slot < 0 || slot >= size() || amount <= 0) return ItemStack.EMPTY;
        ItemStack current = getStack(slot);
        if (current == null || current.isEmpty()) return ItemStack.EMPTY;
        ItemStack removed = current.split(amount);
        setStack(slot, current);
        return removed;
    }

    default ItemStack removeStack(int slot) {
        if (slot < 0 || slot >= size()) return ItemStack.EMPTY;
        ItemStack previous = getStack(slot);
        setStack(slot, ItemStack.EMPTY);
        return previous == null ? ItemStack.EMPTY : previous;
    }

    void setStack(int slot, ItemStack stack);

    default void markDirty() { }

    default boolean canPlayerUse(PlayerEntity player) {
        return player != null && player.isAlive();
    }

    default void onOpen(PlayerEntity player) { }

    default void onClose(PlayerEntity player) { }

    default boolean isValid(int slot, ItemStack stack) {
        return slot >= 0 && slot < size();
    }

    default int getMaxCountPerStack() {
        return 64;
    }

    default int getMaxCount(ItemStack stack) {
        return Math.min(getMaxCountPerStack(), stack == null ? 64 : stack.getMaxCount());
    }

    default int count(Item item) {
        if (item == null) return 0;
        int total = 0;
        for (int slot = 0; slot < size(); slot++) {
            ItemStack stack = getStack(slot);
            if (stack != null && !stack.isEmpty() && stack.isOf(item)) total += stack.getCount();
        }
        return total;
    }

    default boolean containsAny(Set<Item> items) {
        if (items == null || items.isEmpty()) return false;
        for (int slot = 0; slot < size(); slot++) {
            ItemStack stack = getStack(slot);
            if (stack != null && !stack.isEmpty() && items.contains(stack.getItem())) return true;
        }
        return false;
    }

    default boolean containsAny(Set<Item> items, ItemStack stack) {
        return stack != null && !stack.isEmpty() && items != null && items.contains(stack.getItem());
    }

    default boolean containsAny(Predicate<ItemStack> predicate) {
        if (predicate == null) return false;
        for (int slot = 0; slot < size(); slot++) {
            ItemStack stack = getStack(slot);
            if (stack != null && predicate.test(stack)) return true;
        }
        return false;
    }

    default boolean canTransferTo(Inventory hopperInventory, int slot, ItemStack stack) {
        return true;
    }

    default void clear() {
        for (int slot = 0; slot < size(); slot++) setStack(slot, ItemStack.EMPTY);
    }

    static boolean canPlayerUse(BlockEntity blockEntity, PlayerEntity player) {
        return blockEntity != null && player != null && player.isAlive();
    }

    static boolean canPlayerUse(BlockEntity blockEntity, PlayerEntity player, int maxDistance) {
        return canPlayerUse(blockEntity, player);
    }

    static boolean canPlayerUse(BlockEntity blockEntity, PlayerEntity player, float range) {
        return canPlayerUse(blockEntity, player);
    }
}
