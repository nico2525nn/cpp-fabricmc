package net.minecraft.entity.player;

import cppfm.bridge.NativeBridge;
import net.minecraft.item.ItemStack;

/** Handle-backed view of the native player's 36 main, armor, and offhand slots. */
public final class PlayerInventory {
    private final long playerHandle;
    /** Yarn-compatible selected hotbar slot (0..8), sampled at construction. */
    public int selectedSlot;

    public PlayerInventory(long playerHandle) {
        this.playerHandle = playerHandle;
        this.selectedSlot = NativeBridge.nativePlayerHeldSlot(playerHandle);
    }

    public int size() { return 41; }
    public ItemStack getMainHandStack() {
        return ItemStack.fromNative(playerHandle, 27 + selectedSlot);
    }
    public ItemStack getOffHandStack() { return ItemStack.fromNative(playerHandle, 40); }
    public ItemStack getStack(int slot) { return ItemStack.fromNative(playerHandle, slot); }
    public void setStack(int slot, ItemStack stack) {
        if (stack != null) stack.syncCountToNative(playerHandle, slot);
    }
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
