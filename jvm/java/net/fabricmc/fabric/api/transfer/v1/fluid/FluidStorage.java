package net.fabricmc.fabric.api.transfer.v1.fluid;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.fabricmc.fabric.api.lookup.v1.block.BlockApiLookup;
import net.fabricmc.fabric.api.lookup.v1.item.ItemApiLookup;
import net.fabricmc.fabric.api.transfer.v1.context.ContainerItemContext;
import net.fabricmc.fabric.api.transfer.v1.storage.Storage;
import net.minecraft.item.Item;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.Direction;

/** Fluid storage lookup entry points. */
public final class FluidStorage {
    private FluidStorage() { }
    public static final BlockApiLookup<Storage<FluidVariant>, Direction> SIDED = blockLookup();
    public static final ItemApiLookup<Storage<FluidVariant>, ContainerItemContext> ITEM = itemLookup();
    public static final Event<CombinedItemApiProvider> GENERAL_COMBINED_PROVIDER =
        EventFactory.createArrayBacked(CombinedItemApiProvider.class, listeners -> context -> {
            for (CombinedItemApiProvider listener : listeners) {
                Storage<FluidVariant> result = listener.find(context);
                if (result != null) return result;
            }
            return null;
        });
    private static final Map<Item, Event<CombinedItemApiProvider>> ITEM_EVENTS = new ConcurrentHashMap<>();
    @SuppressWarnings({"rawtypes", "unchecked"})
    private static BlockApiLookup<Storage<FluidVariant>, Direction> blockLookup() {
        return (BlockApiLookup) BlockApiLookup.get(Identifier.of("fabric", "fluid_storage"), Storage.class, Direction.class);
    }
    @SuppressWarnings({"rawtypes", "unchecked"})
    private static ItemApiLookup<Storage<FluidVariant>, ContainerItemContext> itemLookup() {
        return (ItemApiLookup) ItemApiLookup.get(Identifier.of("fabric", "fluid_storage_item"), Storage.class, ContainerItemContext.class);
    }
    public static Event<CombinedItemApiProvider> combinedItemApiProvider(Item item) {
        return ITEM_EVENTS.computeIfAbsent(item, ignored -> EventFactory.createArrayBacked(
            CombinedItemApiProvider.class, listeners -> context -> {
                for (CombinedItemApiProvider listener : listeners) {
                    Storage<FluidVariant> result = listener.find(context);
                    if (result != null) return result;
                }
                return null;
            }));
    }
    @FunctionalInterface public interface CombinedItemApiProvider {
        Storage<FluidVariant> find(ContainerItemContext context);
    }
}
