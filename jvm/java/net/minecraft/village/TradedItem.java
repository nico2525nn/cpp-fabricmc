package net.minecraft.village;

import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;

/** Compact item requirement used by a villager trade. */
public class TradedItem {
    private final Item item;
    private final int count;

    public TradedItem(Item item) { this(item, 1); }
    public TradedItem(Item item, int count) {
        this.item = item == null ? net.minecraft.item.Items.AIR : item;
        this.count = Math.max(0, count);
    }
    public TradedItem(ItemStack stack) {
        this(stack == null ? net.minecraft.item.Items.AIR : stack.getItem(), stack == null ? 0 : stack.getCount());
    }
    public Item item() { return item; }
    public int count() { return count; }
    public ItemStack itemStack() { return new ItemStack(item, count); }
    public boolean matches(ItemStack stack) { return stack != null && stack.isOf(item) && stack.getCount() >= count; }
}
