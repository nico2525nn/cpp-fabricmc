package net.fabricmc.fabric.api.transfer.v1.item;

import java.util.List;
import net.fabricmc.fabric.api.transfer.v1.storage.SlottedStorage;
import net.fabricmc.fabric.api.transfer.v1.storage.base.SingleSlotStorage;
import net.minecraft.inventory.Inventory;
import net.minecraft.util.math.Direction;

/** Item-variant view of a vanilla inventory. */
public interface InventoryStorage extends SlottedStorage<ItemVariant> {
    static InventoryStorage of(Inventory inventory, Direction side) {
        return new net.fabricmc.fabric.impl.transfer.item.InventoryStorageImpl(inventory, side);
    }
    @Override List<SingleSlotStorage<ItemVariant>> getSlots();
    @Override default int getSlotCount() { return getSlots().size(); }
    @Override default SingleSlotStorage<ItemVariant> getSlot(int slot) { return getSlots().get(slot); }
}
