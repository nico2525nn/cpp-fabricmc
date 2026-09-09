package net.fabricmc.fabric.api.item.v1;

import net.minecraft.enchantment.Enchantment;
import net.minecraft.item.ItemStack;
import net.minecraft.registry.entry.RegistryEntry;

/** ItemStack extensions injected by Fabric's item API. */
public interface FabricItemStack {
    default ItemStack getRecipeRemainder() {
        return ((ItemStack) this).getItem().getRecipeRemainder() == null
            ? ItemStack.EMPTY : ((ItemStack) this).getItem().getRecipeRemainder().getDefaultStack();
    }

    default boolean canBeEnchantedWith(RegistryEntry<Enchantment> enchantment,
                                       EnchantingContext context) {
        return ((ItemStack) this).isEnchantable();
    }

    default String getCreatorNamespace() {
        RegistryEntry<?> entry = ((ItemStack) this).getRegistryEntry();
        return entry == null || entry.registryKey() == null
            ? "minecraft" : entry.registryKey().getValue().getNamespace();
    }
}
