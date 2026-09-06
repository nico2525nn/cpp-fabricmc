package net.minecraft.inventory;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.util.collection.DefaultedList;

/** Java-only generic inventory with the same slot semantics as vanilla. */
public class SimpleInventory implements Inventory {
    private final int size;
    protected final DefaultedList<ItemStack> heldStacks;
    private final List<InventoryChangedListener> listeners = new ArrayList<>();

    public SimpleInventory(int size) {
        if (size < 0) throw new IllegalArgumentException("Inventory size must be non-negative");
        this.size = size;
        this.heldStacks = DefaultedList.ofSize(size, ItemStack.EMPTY);
    }

    public SimpleInventory(ItemStack... items) {
        this(items == null ? 0 : items.length);
        if (items != null) {
            for (int index = 0; index < items.length; index++)
                heldStacks.set(index, items[index] == null ? ItemStack.EMPTY : items[index]);
        }
    }

    @Override public int size() { return size; }

    @Override public ItemStack getStack(int slot) {
        return slot < 0 || slot >= size ? ItemStack.EMPTY : heldStacks.get(slot);
    }

    @Override public void setStack(int slot, ItemStack stack) {
        if (slot < 0 || slot >= size) return;
        heldStacks.set(slot, stack == null ? ItemStack.EMPTY : stack);
        markDirty();
    }

    @Override public void markDirty() {
        for (InventoryChangedListener listener : List.copyOf(listeners))
            if (listener != null) listener.onInventoryChanged(this);
    }

    public void addListener(InventoryChangedListener listener) {
        if (listener != null) listeners.add(listener);
    }

    public void removeListener(InventoryChangedListener listener) {
        listeners.remove(listener);
    }

    public ItemStack addStack(ItemStack stack) {
        if (stack == null || stack.isEmpty()) return ItemStack.EMPTY;
        for (int slot = 0; slot < size && !stack.isEmpty(); slot++) addToExistingSlot(stack, slot);
        for (int slot = 0; slot < size && !stack.isEmpty(); slot++)
            if (getStack(slot).isEmpty()) addToNewSlot(stack, slot);
        return stack.isEmpty() ? ItemStack.EMPTY : stack;
    }

    private void addToExistingSlot(ItemStack source, int slot) {
        ItemStack target = getStack(slot);
        if (target.isEmpty() || !ItemStack.canCombine(target, source)) return;
        int room = Math.max(0, getMaxCount(target) - target.getCount());
        int amount = Math.min(room, source.getCount());
        if (amount > 0) {
            target.increment(amount);
            source.decrement(amount);
            setStack(slot, target);
        }
    }

    private void addToNewSlot(ItemStack source, int slot) {
        int amount = Math.min(getMaxCount(source), source.getCount());
        setStack(slot, source.copyWithCount(amount));
        source.decrement(amount);
    }

    public void transfer(ItemStack source, ItemStack target) {
        if (source == null || target == null || source.isEmpty() || !ItemStack.canCombine(source, target)) return;
        int amount = Math.min(source.getCount(), Math.max(0, getMaxCount(target) - target.getCount()));
        target.increment(amount);
        source.decrement(amount);
    }

    public boolean canInsert(ItemStack stack) { return stack != null && !stack.isEmpty(); }

    public List<ItemStack> getHeldStacks() { return heldStacks; }

    public List<ItemStack> clearToList() {
        List<ItemStack> result = new ArrayList<>();
        for (int slot = 0; slot < size; slot++) {
            ItemStack stack = removeStack(slot);
            if (stack != null && !stack.isEmpty()) result.add(stack);
        }
        return result;
    }

    public boolean method_24513(ItemStack stack) { return canInsert(stack); }

    public ItemStack removeItem(Item item, int count) {
        if (item == null || count <= 0) return ItemStack.EMPTY;
        ItemStack result = ItemStack.EMPTY;
        int remaining = count;
        for (int slot = 0; slot < size && remaining > 0; slot++) {
            ItemStack current = getStack(slot);
            if (current.isEmpty() || !current.isOf(item)) continue;
            ItemStack removed = removeStack(slot, remaining);
            if (result.isEmpty()) result = removed;
            else result.increment(removed.getCount());
            remaining -= removed.getCount();
        }
        return result;
    }

    public SimpleInventory copy() {
        return new SimpleInventory(heldStacks.stream().map(ItemStack::copy).toArray(ItemStack[]::new));
    }

    public void addAll(Collection<ItemStack> stacks) {
        if (stacks != null) for (ItemStack stack : stacks) addStack(stack);
    }
}
