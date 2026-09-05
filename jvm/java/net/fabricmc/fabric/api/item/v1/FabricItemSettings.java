package net.fabricmc.fabric.api.item.v1;

import net.minecraft.item.FoodComponent;
import net.minecraft.item.Item;
import net.minecraft.item.Rarity;

/** Covariant Fabric settings builder retained as a separate adapter type. */
public class FabricItemSettings extends Item.Settings {
    @Override public FabricItemSettings maxCount(int count) { super.maxCount(count); return this; }
    @Override public FabricItemSettings maxDamage(int damage) { super.maxDamage(damage); return this; }
    @Override public FabricItemSettings fireproof() { super.fireproof(); return this; }
    @Override public FabricItemSettings rarity(Rarity rarity) { super.rarity(rarity); return this; }
    @Override public FabricItemSettings food(FoodComponent food) { super.food(food); return this; }
    @Override public FabricItemSettings recipeRemainder(Item item) { super.recipeRemainder(item); return this; }
    @Override public <T> FabricItemSettings component(net.minecraft.component.DataComponentType<T> type, T value) { super.component(type, value); return this; }
    @Override public FabricItemSettings attributeModifiers(Object modifiers) { super.attributeModifiers(modifiers); return this; }
}
