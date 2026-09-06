package net.minecraft.recipe.display;

import java.util.List;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.registry.entry.RegistryEntry;

/** Factory used when a slot display expands its logical item choices. */
public interface DisplayedItemFactory {
    Object toDisplayed(ItemStack stack);
    Object toDisplayed(Item item);
    Object toDisplayed(RegistryEntry<Item> item);

    interface FromRemainder extends DisplayedItemFactory {
        Object method_64997(Object input, List<?> remainders);
    }

    interface FromStack extends DisplayedItemFactory {
        @Override default Object toDisplayed(ItemStack stack) { return stack; }
        @Override default Object toDisplayed(Item item) { return item; }
        @Override default Object toDisplayed(RegistryEntry<Item> item) { return item; }
    }
}
