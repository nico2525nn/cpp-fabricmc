package net.fabricmc.fabric.api.itemgroup.v1;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.function.Predicate;
import net.minecraft.item.Item;
import net.minecraft.item.ItemConvertible;
import net.minecraft.item.ItemGroup;
import net.minecraft.item.ItemStack;
import net.minecraft.resource.featuretoggle.FeatureSet;

/** Mutable item-group entry collector used by Fabric's item-group callbacks. */
public class FabricItemGroupEntries implements ItemGroup.Entries {
    private final ItemGroup.DisplayContext context;
    private final List<ItemStack> displayStacks;
    private final List<ItemStack> searchTabStacks;

    public FabricItemGroupEntries(ItemGroup.DisplayContext context,
                                  List<ItemStack> displayStacks,
                                  List<ItemStack> searchTabStacks) {
        this.context = context;
        this.displayStacks = displayStacks == null ? new ArrayList<>() : displayStacks;
        this.searchTabStacks = searchTabStacks == null ? new ArrayList<>() : searchTabStacks;
    }

    public ItemGroup.DisplayContext getContext() { return context; }
    public FeatureSet getEnabledFeatures() {
        return context == null || context.enabledFeatures() == null
            ? FeatureSet.EMPTY : context.enabledFeatures();
    }
    public boolean shouldShowOpRestrictedItems() {
        return context != null && context.hasPermissions();
    }
    public List<ItemStack> getDisplayStacks() { return List.copyOf(displayStacks); }
    public List<ItemStack> getSearchTabStacks() { return List.copyOf(searchTabStacks); }

    @Override public void add(ItemStack stack) { add(stack, ItemGroup.StackVisibility.PARENT_AND_SEARCH_TABS); }

    public void add(ItemStack stack, ItemGroup.StackVisibility visibility) {
        if (stack == null || stack.isEmpty()) return;
        ItemGroup.StackVisibility actual = visibility == null
            ? ItemGroup.StackVisibility.PARENT_AND_SEARCH_TABS : visibility;
        if (actual != ItemGroup.StackVisibility.SEARCH_TAB_ONLY) displayStacks.add(stack.copy());
        if (actual != ItemGroup.StackVisibility.PARENT_TAB_ONLY) searchTabStacks.add(stack.copy());
    }

    public void addAll(Collection<ItemStack> stacks) { addAll(stacks, ItemGroup.StackVisibility.PARENT_AND_SEARCH_TABS); }
    public void addAll(Collection<ItemStack> stacks, ItemGroup.StackVisibility visibility) {
        if (stacks != null) for (ItemStack stack : stacks) add(stack, visibility);
    }
    @Override public void add(Item item) { if (item != null) add(item.getDefaultStack()); }
    @Override public void add(Item item, ItemGroup.StackVisibility visibility) {
        if (item != null) add(item.getDefaultStack(), visibility);
    }
    public void prepend(ItemStack stack) { prepend(stack, ItemGroup.StackVisibility.PARENT_AND_SEARCH_TABS); }
    public void prepend(ItemStack stack, ItemGroup.StackVisibility visibility) {
        if (stack == null || stack.isEmpty()) return;
        if (visibility != ItemGroup.StackVisibility.SEARCH_TAB_ONLY) displayStacks.add(0, stack.copy());
        if (visibility != ItemGroup.StackVisibility.PARENT_TAB_ONLY) searchTabStacks.add(0, stack.copy());
    }
    public void prepend(ItemConvertible item) { if (item != null) prepend(item.asItem().getDefaultStack()); }
    public void prepend(ItemConvertible item, ItemGroup.StackVisibility visibility) {
        if (item != null) prepend(item.asItem().getDefaultStack(), visibility);
    }
    public void addAfter(ItemConvertible existing, ItemStack... stacks) { insertAfter(existing, List.of(stacks)); }
    public void addAfter(ItemStack existing, ItemStack... stacks) { insertAfter(existing, List.of(stacks)); }
    public void addAfter(ItemConvertible existing, ItemConvertible... items) { insertAfter(existing, stacks(items)); }
    public void addAfter(ItemStack existing, ItemConvertible... items) { insertAfter(existing, stacks(items)); }
    public void addAfter(ItemConvertible existing, Collection<ItemStack> stacks) { insertAfter(existing, stacks); }
    public void addAfter(ItemStack existing, Collection<ItemStack> stacks) { insertAfter(existing, stacks); }
    public void addAfter(ItemConvertible existing, Collection<ItemStack> stacks,
                         ItemGroup.StackVisibility visibility) {
        insertAfter(existing == null ? null : existing.asItem().getDefaultStack(), stacks, visibility);
    }
    public void addAfter(ItemStack existing, Collection<ItemStack> stacks,
                         ItemGroup.StackVisibility visibility) {
        insertAfter(existing, stacks, visibility);
    }
    public void addAfter(Predicate<ItemStack> predicate, Collection<ItemStack> stacks,
                         ItemGroup.StackVisibility visibility) {
        int index = indexOf(displayStacks, predicate);
        if (index < 0) { addAll(stacks, visibility); return; }
        insertAt(displayStacks, index + 1, stacks, visibility, true);
    }
    public void addBefore(ItemConvertible existing, ItemStack... stacks) { insertBefore(existing, List.of(stacks)); }
    public void addBefore(ItemStack existing, ItemStack... stacks) { insertBefore(existing, List.of(stacks)); }
    public void addBefore(ItemConvertible existing, ItemConvertible... items) { insertBefore(existing, stacks(items)); }
    public void addBefore(ItemStack existing, ItemConvertible... items) { insertBefore(existing, stacks(items)); }
    public void addBefore(ItemConvertible existing, Collection<ItemStack> stacks) { insertBefore(existing, stacks); }
    public void addBefore(ItemStack existing, Collection<ItemStack> stacks) { insertBefore(existing, stacks); }
    public void addBefore(ItemConvertible existing, Collection<ItemStack> stacks,
                          ItemGroup.StackVisibility visibility) {
        insertBefore(existing == null ? null : existing.asItem().getDefaultStack(), stacks, visibility);
    }
    public void addBefore(ItemStack existing, Collection<ItemStack> stacks,
                          ItemGroup.StackVisibility visibility) {
        insertBefore(existing, stacks, visibility);
    }
    public void addBefore(Predicate<ItemStack> predicate, Collection<ItemStack> stacks,
                          ItemGroup.StackVisibility visibility) {
        int index = indexOf(displayStacks, predicate);
        if (index < 0) { addAll(stacks, visibility); return; }
        insertAt(displayStacks, index, stacks, visibility, true);
    }

    /** Intermediary-named method retained because Fabric 1.21.4 leaves it unmapped. */
    public void method_45417(ItemStack stack, ItemGroup.StackVisibility visibility) { add(stack, visibility); }

    private void insertAfter(ItemConvertible existing, Collection<ItemStack> stacks) {
        insertAfter(existing == null ? null : existing.asItem().getDefaultStack(), stacks,
            ItemGroup.StackVisibility.PARENT_AND_SEARCH_TABS);
    }
    private void insertAfter(ItemStack existing, Collection<ItemStack> stacks) {
        insertAfter(existing, stacks, ItemGroup.StackVisibility.PARENT_AND_SEARCH_TABS);
    }
    private void insertAfter(ItemStack existing, Collection<ItemStack> stacks,
                             ItemGroup.StackVisibility visibility) {
        int index = indexOf(displayStacks, existing);
        if (index < 0) { addAll(stacks, visibility); return; }
        insertAt(displayStacks, index + 1, stacks, visibility, true);
    }
    private void insertBefore(ItemConvertible existing, Collection<ItemStack> stacks) {
        insertBefore(existing == null ? null : existing.asItem().getDefaultStack(), stacks,
            ItemGroup.StackVisibility.PARENT_AND_SEARCH_TABS);
    }
    private void insertBefore(ItemStack existing, Collection<ItemStack> stacks) {
        insertBefore(existing, stacks, ItemGroup.StackVisibility.PARENT_AND_SEARCH_TABS);
    }
    private void insertBefore(ItemStack existing, Collection<ItemStack> stacks,
                              ItemGroup.StackVisibility visibility) {
        int index = indexOf(displayStacks, existing);
        if (index < 0) { addAll(stacks, visibility); return; }
        insertAt(displayStacks, index, stacks, visibility, true);
    }
    private void insertAt(List<ItemStack> target, int index, Collection<ItemStack> stacks,
                          ItemGroup.StackVisibility visibility, boolean copy) {
        if (stacks == null) return;
        int offset = 0;
        for (ItemStack stack : stacks) {
            if (stack == null || stack.isEmpty()) continue;
            if (visibility != ItemGroup.StackVisibility.SEARCH_TAB_ONLY)
                target.add(Math.min(index + offset, target.size()), copy ? stack.copy() : stack);
            if (visibility != ItemGroup.StackVisibility.PARENT_TAB_ONLY) searchTabStacks.add(copy ? stack.copy() : stack);
            offset++;
        }
    }
    private static int indexOf(List<ItemStack> values, ItemStack wanted) {
        if (wanted == null) return -1;
        for (int index = 0; index < values.size(); index++)
            if (ItemStack.areItemsEqual(values.get(index), wanted)) return index;
        return -1;
    }
    private static int indexOf(List<ItemStack> values, Predicate<ItemStack> predicate) {
        if (predicate == null) return -1;
        for (int index = 0; index < values.size(); index++) if (predicate.test(values.get(index))) return index;
        return -1;
    }
    private static List<ItemStack> stacks(ItemConvertible... items) {
        List<ItemStack> values = new ArrayList<>();
        if (items != null) for (ItemConvertible item : items) if (item != null) values.add(item.asItem().getDefaultStack());
        return values;
    }
}
