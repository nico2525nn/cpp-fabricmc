package net.fabricmc.fabric.impl.transfer.item;

import java.util.List;
import net.fabricmc.fabric.api.transfer.v1.context.ContainerItemContext;
import net.fabricmc.fabric.api.transfer.v1.item.ItemVariant;
import net.fabricmc.fabric.api.transfer.v1.storage.base.SingleSlotStorage;

public final class ContainerItemContextImpl implements ContainerItemContext {
    private final SingleSlotStorage<ItemVariant> main;
    private final List<SingleSlotStorage<ItemVariant>> additional;
    public ContainerItemContextImpl(SingleSlotStorage<ItemVariant> main, List<SingleSlotStorage<ItemVariant>> additional) {
        this.main = java.util.Objects.requireNonNull(main, "main");
        this.additional = additional == null ? List.of() : List.copyOf(additional);
    }
    @Override public ItemVariant getItemVariant() { return main.getResource(); }
    @Override public long getAmount() { return main.getAmount(); }
    @Override public SingleSlotStorage<ItemVariant> getMainSlot() { return main; }
    @Override public List<SingleSlotStorage<ItemVariant>> getAdditionalSlots() { return additional; }
}
