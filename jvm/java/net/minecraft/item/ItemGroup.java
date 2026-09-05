package net.minecraft.item;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.text.MutableText;
import net.minecraft.text.Text;
import net.minecraft.util.Identifier;

public final class ItemGroup {
    private final String id;
    private final List<ItemStack> entries = new ArrayList<>();
    public ItemGroup(String id) { this.id = id == null ? "" : id; }
    public String id() { return id; }
    public Identifier getId() { return Identifier.of("minecraft", id); }
    public Text getDisplayName() { return Text.translatable("itemGroup." + id); }
    public ItemStack getIcon() { return entries.isEmpty() ? ItemStack.EMPTY : entries.get(0); }
    public List<ItemStack> getEntries() { return List.copyOf(entries); }
    public void add(ItemStack stack) { if (stack != null && !stack.isEmpty()) entries.add(stack.copy()); }
    public void add(Item item) { if (item != null) add(item.getDefaultStack()); }
    public enum Row { TOP, BOTTOM }
    public enum EntryPosition { ABOVE, BELOW }
    public enum StackVisibility { PARENT_AND_SEARCH_TABS, PARENT_TAB_ONLY, SEARCH_TAB_ONLY }
    @Override public String toString() { return id; }
}
