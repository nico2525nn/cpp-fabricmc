package net.fabricmc.fabric.api.transfer.v1.fluid.base;

import java.util.List;
import java.util.function.Function;
import net.fabricmc.fabric.api.transfer.v1.context.ContainerItemContext;
import net.fabricmc.fabric.api.transfer.v1.fluid.FluidVariant;
import net.fabricmc.fabric.api.transfer.v1.item.ItemVariant;
import net.fabricmc.fabric.api.transfer.v1.storage.StorageView;
import net.fabricmc.fabric.api.transfer.v1.storage.base.BlankVariantView;
import net.fabricmc.fabric.api.transfer.v1.storage.base.InsertionOnlyStorage;
import net.fabricmc.fabric.api.transfer.v1.transaction.TransactionContext;
import net.minecraft.item.Item;

/** Fluid storage for an empty container that becomes a full item on insert. */
public final class EmptyItemFluidStorage implements InsertionOnlyStorage<FluidVariant> {
    private final ContainerItemContext context;
    private final Function<ItemVariant, ItemVariant> emptyToFull;
    private final net.minecraft.fluid.Fluid fluid;
    private final long amount;
    private final List<StorageView<FluidVariant>> views;
    public EmptyItemFluidStorage(ContainerItemContext context, Item emptyItem,
                                 net.minecraft.fluid.Fluid fluid, long amount) {
        this(context, variant -> ItemVariant.of(emptyItem), fluid, amount);
    }
    public EmptyItemFluidStorage(ContainerItemContext context,
                                 Function<ItemVariant, ItemVariant> emptyToFull,
                                 net.minecraft.fluid.Fluid fluid, long amount) {
        this.context = java.util.Objects.requireNonNull(context, "context");
        this.emptyToFull = java.util.Objects.requireNonNull(emptyToFull, "emptyToFull");
        this.fluid = java.util.Objects.requireNonNull(fluid, "fluid");
        this.amount = Math.max(0, amount);
        this.views = List.of(new BlankVariantView<>(FluidVariant.blank(), this.amount));
    }
    @Override public long insert(FluidVariant resource, long max, TransactionContext tx) {
        if (resource == null || resource.isBlank() || !resource.isOf(fluid)) return 0;
        long accepted = Math.min(max, amount);
        if (accepted <= 0) return 0;
        ItemVariant replacement = emptyToFull.apply(context.getItemVariant());
        return context.exchange(replacement, 1, tx) == 1 ? accepted : 0;
    }
    @Override public java.util.Iterator<StorageView<FluidVariant>> iterator() { return views.iterator(); }
    @Override public String toString() { return "EmptyItemFluidStorage[" + fluid + "," + amount + "]"; }
}
