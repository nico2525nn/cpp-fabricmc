package net.fabricmc.fabric.api.transfer.v1.item.base;

import net.fabricmc.fabric.api.transfer.v1.item.ItemVariant;
import net.fabricmc.fabric.api.transfer.v1.storage.base.SingleVariantStorage;

/** SingleStackStorage specialization with the blank item variant defined. */
public abstract class SingleItemStorage extends SingleVariantStorage<ItemVariant> {
    public SingleItemStorage() { }
    @Override protected final ItemVariant getBlankVariant() { return ItemVariant.blank(); }
    public void readNbt(net.minecraft.nbt.NbtCompound nbt, net.minecraft.registry.RegistryWrapper.WrapperLookup lookup) { }
    public void writeNbt(net.minecraft.nbt.NbtCompound nbt, net.minecraft.registry.RegistryWrapper.WrapperLookup lookup) { }
}
