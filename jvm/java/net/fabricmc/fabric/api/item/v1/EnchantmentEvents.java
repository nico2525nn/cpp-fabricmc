package net.fabricmc.fabric.api.item.v1;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.fabricmc.fabric.api.util.TriState;
import net.minecraft.enchantment.Enchantment;
import net.minecraft.item.ItemStack;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.entry.RegistryEntry;

/** Server-safe enchantment registration and applicability events. */
public final class EnchantmentEvents {
    public static final Event<AllowEnchanting> ALLOW_ENCHANTING = EventFactory.createArrayBacked(
        AllowEnchanting.class, callbacks -> (enchantment, stack, context) -> {
            for (AllowEnchanting callback : callbacks) {
                TriState result = callback.allowEnchanting(enchantment, stack, context);
                if (result != null && result != TriState.DEFAULT) return result;
            }
            return TriState.DEFAULT;
        });

    public static final Event<Modify> MODIFY = EventFactory.createArrayBacked(
        Modify.class, callbacks -> (key, builder, source) -> {
            for (Modify callback : callbacks) callback.modify(key, builder, source);
        });

    private EnchantmentEvents() { }

    @FunctionalInterface
    public interface AllowEnchanting {
        TriState allowEnchanting(RegistryEntry<Enchantment> enchantment,
                                 ItemStack stack, EnchantingContext context);
    }

    @FunctionalInterface
    public interface Modify {
        void modify(RegistryKey<Enchantment> key, Enchantment.Builder builder,
                    EnchantmentSource source);
    }
}
