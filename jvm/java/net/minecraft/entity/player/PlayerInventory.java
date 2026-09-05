package net.minecraft.entity.player;

import java.util.List;
import net.minecraft.item.ItemStack;
import net.minecraft.item.Items;
import net.minecraft.util.NativeAccess;
import net.minecraft.util.collection.DefaultedList;

/** Handle-backed view of the native player's 36 main, armor, and offhand slots. */
public class PlayerInventory {
    private final long playerHandle;
    private final ItemStack[] localSlots;
    /** Yarn-compatible selected hotbar slot (0..8), sampled at construction. */
    public int selectedSlot;
    public final DefaultedList<ItemStack> main;
    public final DefaultedList<ItemStack> armor;
    public final DefaultedList<ItemStack> offHand;

    public PlayerInventory(long playerHandle) {
        this.playerHandle = playerHandle;
        this.localSlots = playerHandle == 0L ? new ItemStack[41] : null;
        if (localSlots != null) java.util.Arrays.fill(localSlots, ItemStack.EMPTY);
        this.selectedSlot = NativeAccess.heldSlot(playerHandle);
        this.main = DefaultedList.ofSize(36, ItemStack.EMPTY, index -> getStack(index), this::setStack);
        this.armor = DefaultedList.ofSize(4, ItemStack.EMPTY, index -> getStack(36 + index), (index, stack) -> setStack(36 + index, stack));
        this.offHand = DefaultedList.ofSize(1, ItemStack.EMPTY, index -> getStack(40), (index, stack) -> setStack(40, stack));
    }

    public int size() { return 41; }
    public ItemStack getMainHandStack() {
        return getStack(27 + Math.max(0, Math.min(8, selectedSlot)));
    }
    public ItemStack getOffHandStack() { return getStack(40); }
    public ItemStack getStack(int slot) {
        if (slot < 0 || slot >= size()) return ItemStack.EMPTY;
        return playerHandle == 0L ? localSlots[slot] : ItemStack.fromNative(playerHandle, slot);
    }
    public void setStack(int slot, ItemStack stack) {
        if (slot < 0 || slot >= size()) return;
        ItemStack value = stack == null ? ItemStack.EMPTY : stack;
        if (playerHandle == 0L) localSlots[slot] = value;
        else value.syncCountToNative(playerHandle, slot);
    }
    public ItemStack removeStack(int slot) { ItemStack old = getStack(slot); setStack(slot, ItemStack.EMPTY); return old; }
    public ItemStack removeStack(int slot, int amount) {
        if (amount <= 0) return ItemStack.EMPTY;
        ItemStack current = getStack(slot);
        if (current.isEmpty()) return ItemStack.EMPTY;
        ItemStack removed = current.split(amount); setStack(slot, current); return removed;
    }
    public void setSelectedSlot(int slot) { selectedSlot = Math.max(0, Math.min(8, slot)); }
    public int getSelectedSlot() { return selectedSlot; }
    public int getEmptySlot() { for (int i = 0; i < size(); i++) if (getStack(i).isEmpty()) return i; return -1; }
    public boolean insertStack(ItemStack stack) {
        if (stack == null || stack.isEmpty()) return true;
        int slot = getEmptySlot(); if (slot < 0) return false; setStack(slot, stack); return true;
    }
    public boolean insertStack(int slot, ItemStack stack) {
        if (slot < 0 || slot >= size() || stack == null || stack.isEmpty() || !getStack(slot).isEmpty()) return false;
        setStack(slot, stack); return true;
    }
    public boolean contains(ItemStack stack) { return count(stack) > 0; }
    public boolean contains(net.minecraft.item.Item item) { return item != null && count(new ItemStack(item)) > 0; }
    public boolean isEmpty() { for (int i = 0; i < size(); i++) if (!getStack(i).isEmpty()) return false; return true; }
    public void clear() { for (int i = 0; i < size(); i++) setStack(i, ItemStack.EMPTY); }
    public void markDirty() { }
    public boolean canPlayerUse(PlayerEntity player) { return player != null && player.isAlive(); }
    public List<ItemStack> getHeldStacks() { return List.of(getMainHandStack(), getOffHandStack()); }
    public int count(ItemStack stack) {
        if (stack == null || stack.isEmpty()) return 0;
        int total = 0;
        for (int slot = 0; slot < size(); ++slot) {
            ItemStack candidate = getStack(slot);
            if (candidate.isOf(stack.getItem())) total += candidate.getCount();
        }
        return total;
    }
    public long nativeHandle() { return playerHandle; }
}
