package net.fabricmc.fabric.api.transfer.v1.item;

import com.mojang.serialization.Codec;
import java.util.Objects;
import net.fabricmc.fabric.api.transfer.v1.storage.TransferVariant;
import net.minecraft.component.ComponentChanges;
import net.minecraft.item.Item;
import net.minecraft.item.ItemConvertible;
import net.minecraft.item.ItemStack;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.registry.entry.RegistryEntry;

/** Immutable, count-less item stack used by Transfer API. */
public interface ItemVariant extends TransferVariant<Item> {
    Codec<ItemVariant> CODEC = new Codec<>() { };
    PacketCodec<RegistryByteBuf, ItemVariant> PACKET_CODEC = PacketCodec.ofLegacy((buffer, value) -> { }, buffer -> blank());

    static ItemVariant blank() { return of(net.minecraft.item.Items.AIR); }
    static ItemVariant of(ItemStack stack) {
        return stack == null ? blank() : of(stack.getItem(), stack.getComponentChanges());
    }
    static ItemVariant of(ItemConvertible item) { return of(item, ComponentChanges.EMPTY); }
    static ItemVariant of(ItemConvertible item, ComponentChanges components) {
        return new net.fabricmc.fabric.impl.transfer.item.ItemVariantImpl(
            item == null ? net.minecraft.item.Items.AIR : item.asItem(),
            components == null ? ComponentChanges.EMPTY : components);
    }
    default boolean matches(ItemStack stack) {
        return stack != null && isOf(stack.getItem()) && Objects.equals(stack.getComponentChanges(), getComponents());
    }
    default Item getItem() { return getObject(); }
    default RegistryEntry<Item> getRegistryEntry() { return getItem().getRegistryEntry(); }
    default ItemStack toStack() { return toStack(1); }
    default ItemStack toStack(int count) {
        return isBlank() ? ItemStack.EMPTY : applyChanges(new ItemStack(getItem(), count));
    }
    @Override ItemVariant withComponentChanges(ComponentChanges changes);

    private ItemStack applyChanges(ItemStack stack) {
        stack.applyChanges(getComponents());
        return stack;
    }
}
