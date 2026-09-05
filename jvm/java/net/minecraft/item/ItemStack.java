package net.minecraft.item;

import net.minecraft.text.Text;

public class ItemStack {
    public static final ItemStack EMPTY = new ItemStack();
    private final Item item;
    private int count;
    private long nativeOwner;
    private int nativeSlot = -1;
    private ItemStack() { item = new Item(); count = 0; }
    public ItemStack(Item item) { this(item, 1); }
    public ItemStack(Item item, int count) { this.item = item == null ? new Item() : item; this.count = Math.max(0, count); }
    public static ItemStack fromNative(long playerHandle, int slot) {
        int rawId = cppfm.bridge.NativeBridge.nativePlayerInventoryItemId(playerHandle, slot);
        int count = cppfm.bridge.NativeBridge.nativePlayerInventoryItemCount(playerHandle, slot);
        String name = cppfm.bridge.NativeBridge.nativePlayerInventoryItemName(playerHandle, slot);
        ItemStack result = new ItemStack(Item.fromRaw(rawId, name), count);
        result.nativeOwner = playerHandle;
        result.nativeSlot = slot;
        return result;
    }
    public boolean isEmpty() { return count <= 0; }
    public Item getItem() { return item; }
    public int getCount() { return count; }
    public void setCount(int count) {
        this.count = Math.max(0, count);
        syncCountToNative(nativeOwner, nativeSlot);
    }
    public void decrement(int amount) { setCount(Math.max(0, count - Math.max(0, amount))); }
    public void increment(int amount) { setCount(count + Math.max(0, amount)); }
    public void syncCountToNative(long owner, int slot) {
        if (owner != 0 && slot >= 0) {
            cppfm.bridge.NativeBridge.nativePlayerSetInventoryItem(
                owner, slot, item.getRawId(), count);
            nativeOwner = owner;
            nativeSlot = slot;
        }
    }
    public ItemStack copy() { return new ItemStack(item, count); }
    public Text getName() { return Text.literal(item.getId().toString()); }
    public boolean isOf(Item other) { return item == other || (other != null && item.getId().equals(other.getId())); }
}
