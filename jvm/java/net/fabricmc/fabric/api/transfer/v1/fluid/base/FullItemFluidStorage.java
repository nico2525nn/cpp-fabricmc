package net.fabricmc.fabric.api.transfer.v1.fluid.base;

import java.util.function.Function;
import net.fabricmc.fabric.api.transfer.v1.context.ContainerItemContext;
import net.fabricmc.fabric.api.transfer.v1.fluid.FluidVariant;
import net.fabricmc.fabric.api.transfer.v1.item.ItemVariant;
import net.fabricmc.fabric.api.transfer.v1.storage.StoragePreconditions;
import net.fabricmc.fabric.api.transfer.v1.storage.base.SingleSlotStorage;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;
import net.minecraft.item.Item;

/** Fluid extraction view for a full container item. */
public final class FullItemFluidStorage implements net.fabricmc.fabric.api.transfer.v1.storage.base.ExtractionOnlyStorage<FluidVariant>, SingleSlotStorage<FluidVariant> {
    private final ContainerItemContext context;
    private final Function<ItemVariant, ItemVariant> fullToEmpty;
    private final FluidVariant fluid;
    private final long amount;
    public FullItemFluidStorage(ContainerItemContext context, Item fullItem, FluidVariant fluid, long amount) {
        this(context, variant -> ItemVariant.of(fullItem), fluid, amount);
    }
    public FullItemFluidStorage(ContainerItemContext context,
                                Function<ItemVariant, ItemVariant> fullToEmpty,
                                FluidVariant fluid, long amount) {
        this.context = java.util.Objects.requireNonNull(context, "context");
        this.fullToEmpty = java.util.Objects.requireNonNull(fullToEmpty, "fullToEmpty");
        this.fluid = java.util.Objects.requireNonNull(fluid, "fluid");
        this.amount = Math.max(0, amount);
    }
    @Override public long extract(FluidVariant resource, long max, TransactionContext tx) {
        StoragePreconditions.notBlankNotNegative(resource, max);
        if (!resource.equals(fluid)) return 0;
        long removed = Math.min(max, amount);
        if (removed <= 0) return 0;
        ItemVariant replacement = fullToEmpty.apply(context.getItemVariant());
        return context.exchange(replacement, 1, tx) == 1 ? removed : 0;
    }
    @Override public boolean isResourceBlank() { return fluid.isBlank() || amount == 0; }
    @Override public FluidVariant getResource() { return fluid; }
    @Override public long getAmount() { return amount; }
    @Override public long getCapacity() { return amount; }
    @Override public String toString() { return "FullItemFluidStorage[" + fluid + "," + amount + "]"; }
}
