package net.fabricmc.fabric.impl.transfer.item;

import java.util.Objects;
import net.fabricmc.fabric.api.transfer.v1.item.ItemVariant;
import net.minecraft.component.ComponentChanges;
import net.minecraft.component.ComponentMap;
import net.minecraft.item.Item;

/** Identity/value implementation for ItemVariant. */
public final class ItemVariantImpl implements ItemVariant {
    private final Item item;
    private final ComponentChanges components;
    public ItemVariantImpl(Item item, ComponentChanges components) {
        this.item = item == null ? net.minecraft.item.Items.AIR : item;
        this.components = components == null ? ComponentChanges.EMPTY : components;
    }
    @Override public boolean isBlank() { return item == net.minecraft.item.Items.AIR; }
    @Override public Item getObject() { return item; }
    @Override public ComponentChanges getComponents() { return components; }
    @Override public ComponentMap getComponentMap() { return components.toAddedRemovedPair().added(); }
    @Override public ItemVariant withComponentChanges(ComponentChanges changes) { return new ItemVariantImpl(item, changes); }
    @Override public boolean equals(Object other) {
        return other instanceof ItemVariant value && item == value.getItem() && components.equals(value.getComponents());
    }
    @Override public int hashCode() { return Objects.hash(System.identityHashCode(item), components); }
    @Override public String toString() { return "ItemVariant[" + item + "," + components + "]"; }
}
