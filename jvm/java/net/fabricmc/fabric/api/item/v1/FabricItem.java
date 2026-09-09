package net.fabricmc.fabric.api.item.v1;

import net.minecraft.enchantment.Enchantment;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.item.ItemStack;
import net.minecraft.item.Item;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.util.Identifier;
import net.minecraft.util.Hand;

/**
 * Fabric's item extension interface. It is mixed into Item at runtime; it
 * must remain an interface so the real Fabric item mixins link correctly.
 */
public interface FabricItem {
    /** Extra settings methods mixed into vanilla {@code Item.Settings}. */
    interface Settings {
        default Item.Settings equipmentSlot(EquipmentSlotProvider provider) {
            return (Item.Settings) this;
        }

        default Item.Settings customDamage(CustomDamageHandler handler) {
            return (Item.Settings) this;
        }

        default Item.Settings modelId(Identifier modelId) {
            return (Item.Settings) this;
        }
    }

    default boolean allowComponentsUpdateAnimation(PlayerEntity player, Hand hand,
                                                   ItemStack oldStack, ItemStack newStack) {
        return true;
    }

    default boolean allowContinuingBlockBreaking(PlayerEntity player,
                                                 ItemStack oldStack, ItemStack newStack) {
        return false;
    }

    default ItemStack getRecipeRemainder(ItemStack stack) {
        return ItemStack.EMPTY;
    }

    default boolean canBeEnchantedWith(ItemStack stack, RegistryEntry<Enchantment> enchantment,
                                       EnchantingContext context) {
        return false;
    }

    default String getCreatorNamespace(ItemStack stack) {
        return "minecraft";
    }
}
