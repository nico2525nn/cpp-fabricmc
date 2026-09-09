package net.fabricmc.fabric.api.transfer.v1.storage.base;

import net.fabricmc.fabric.api.transfer.v1.fluid.FluidVariant;
import net.fabricmc.fabric.api.transfer.v1.item.ItemVariant;
import net.fabricmc.fabric.api.transfer.v1.storage.Storage;
import net.minecraft.util.math.Direction;

/**
 * Optional convenience interface for block entities that expose sided
 * Transfer API storages.  The lookup providers call these methods only after
 * checking the block entity instance, so the default implementation is a
 * deliberately safe "not exposed" result.
 */
public interface SidedStorageBlockEntity {
    default Storage<FluidVariant> getFluidStorage(Direction side) {
        return null;
    }

    default Storage<ItemVariant> getItemStorage(Direction side) {
        return null;
    }
}
