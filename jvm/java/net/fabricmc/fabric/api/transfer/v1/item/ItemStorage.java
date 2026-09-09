package net.fabricmc.fabric.api.transfer.v1.item;

import net.fabricmc.fabric.api.lookup.v1.block.BlockApiLookup;
import net.fabricmc.fabric.api.lookup.v1.item.ItemApiLookup;
import net.fabricmc.fabric.api.transfer.v1.context.ContainerItemContext;
import net.fabricmc.fabric.api.transfer.v1.storage.Storage;
import net.fabricmc.fabric.api.transfer.v1.storage.base.SidedStorageBlockEntity;
import net.minecraft.inventory.Inventory;
import net.minecraft.item.Item;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.Direction;

/** Global lookup entry points for item Transfer API storages. */
public final class ItemStorage {
    private ItemStorage() { }

    @SuppressWarnings({"rawtypes", "unchecked"})
    public static final BlockApiLookup<Storage<ItemVariant>, Direction> SIDED =
        (BlockApiLookup) BlockApiLookup.get(Identifier.of("fabric", "sided_item_storage"),
            Storage.class, Direction.class);
    @SuppressWarnings({"rawtypes", "unchecked"})
    public static final ItemApiLookup<Storage<ItemVariant>, ContainerItemContext> ITEM =
        (ItemApiLookup) ItemApiLookup.get(Identifier.of("fabric", "item_storage"),
            Storage.class, ContainerItemContext.class);

    static {
        SIDED.registerFallback((world, pos, state, blockEntity, side) -> {
            if (blockEntity instanceof SidedStorageBlockEntity sided) {
                Storage<ItemVariant> storage = sided.getItemStorage(side);
                if (storage != null) return storage;
            }
            return blockEntity instanceof Inventory inventory
                ? InventoryStorage.of(inventory, side) : null;
        });
        ITEM.registerFallback((stack, context) -> null);
    }

    /** Register a provider for a custom item without exposing implementation details. */
    public static void registerForItem(Item item,
                                       ItemApiLookup.ItemApiProvider<Storage<ItemVariant>, ContainerItemContext> provider) {
        if (item != null && provider != null) ITEM.registerForItems(provider, item);
    }
}
