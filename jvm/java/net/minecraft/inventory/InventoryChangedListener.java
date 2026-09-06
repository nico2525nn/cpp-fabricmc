package net.minecraft.inventory;

/** Listener notified after an inventory changes. */
@FunctionalInterface
public interface InventoryChangedListener {
    void onInventoryChanged(Inventory inventory);
}
