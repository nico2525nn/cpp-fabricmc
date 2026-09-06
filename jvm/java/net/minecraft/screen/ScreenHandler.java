package net.minecraft.screen;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.entity.player.PlayerInventory;
import net.minecraft.item.ItemStack;
import net.minecraft.screen.slot.Slot;
import net.minecraft.screen.slot.SlotActionType;

/** Core screen-handler ABI for server-side inventory mixins. */
public abstract class ScreenHandler {
    protected final ScreenHandlerType<?> type;
    public final int syncId;
    protected final List<Slot> slots = new ArrayList<>();
    protected ItemStack cursorStack = ItemStack.EMPTY;

    protected ScreenHandler(ScreenHandlerType<?> type, int syncId) { this.type = type; this.syncId = syncId; }
    public Slot addSlot(Slot slot) { if (slot != null) slots.add(slot); return slot; }
    public Slot getSlot(int index) { return index < 0 || index >= slots.size() ? null : slots.get(index); }
    public List<Slot> getSlots() { return slots; }
    public ItemStack getCursorStack() { return cursorStack; }
    public void setCursorStack(ItemStack stack) { cursorStack = stack == null ? ItemStack.EMPTY : stack; }
    public boolean canUse(PlayerEntity player) { return true; }
    public void onSlotClick(int slotIndex, int button, SlotActionType actionType, PlayerEntity player) { }
    public void doClick(int slotIndex, int button, SlotActionType actionType, PlayerEntity player) {
        onSlotClick(slotIndex, button, actionType, player);
    }
    /** Yarn name for the internal slot-click dispatch point. */
    public void internalOnSlotClick(int slotIndex, int button, SlotActionType actionType, PlayerEntity player) {
        doClick(slotIndex, button, actionType, player);
    }
    public void onClosed(PlayerEntity player) { }
    public void addPlayerSlots(PlayerInventory inventory, int left, int top) { }
    public boolean insertItem(ItemStack stack, int startIndex, int endIndex, boolean fromLast) { return false; }
    public ItemStack quickMove(PlayerEntity player, int slot) { return ItemStack.EMPTY; }
    public void sendContentUpdates() { }
    public boolean onButtonClick(PlayerEntity player, int id) { return false; }
    public static int calculateComparatorOutput(net.minecraft.inventory.Inventory inventory) {
        if (inventory == null || inventory.size() == 0) return 0;
        int occupied = 0;
        for (int i = 0; i < inventory.size(); ++i)
            if (inventory.getStack(i) != null && !inventory.getStack(i).isEmpty()) ++occupied;
        return Math.min(15, Math.round(occupied * 15.0f / inventory.size()));
    }
}
