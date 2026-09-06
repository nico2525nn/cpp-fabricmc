package net.minecraft.screen.slot;

import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.inventory.Inventory;
import net.minecraft.item.ItemStack;

/** Slot ABI used by screen-handler mixins. */
public class Slot {
    protected final Inventory inventory;
    public final int id;
    public final int x;
    public final int y;
    public Slot(Inventory inventory, int index, int x, int y) { this.inventory = inventory; this.id = index; this.x = x; this.y = y; }
    public ItemStack getStack() { return inventory == null ? ItemStack.EMPTY : inventory.getStack(id); }
    public void setStack(ItemStack stack) { if (inventory != null) inventory.setStack(id, stack); }
    public boolean hasStack() { return !getStack().isEmpty(); }
    public boolean canInsert(ItemStack stack) { return true; }
    public boolean canTakeItems(PlayerEntity player) { return true; }
    public void markDirty() { if (inventory != null) inventory.markDirty(); }
    public ItemStack takeStack(int amount) { return inventory == null ? ItemStack.EMPTY : inventory.removeStack(id, amount); }
}
