package net.fabricmc.fabric.api.recipe.v1.ingredient;

import java.util.List;
import java.util.Objects;
import java.util.stream.Stream;
import net.fabricmc.fabric.impl.recipe.ingredient.CustomIngredientImpl;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.recipe.Ingredient;
import net.minecraft.recipe.display.SlotDisplay;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.util.context.ContextParameterMap;

/** Extensible recipe ingredient with stack-sensitive matching. */
public interface CustomIngredient {
    boolean test(ItemStack stack);
    Stream<RegistryEntry<Item>> getMatchingItems();
    boolean requiresTesting();
    CustomIngredientSerializer<?> getSerializer();

    default SlotDisplay toDisplay() {
        List<ItemStack> stacks = getMatchingItems() == null ? List.of()
            : getMatchingItems().filter(Objects::nonNull).map(ItemStack::new).toList();
        return new SlotDisplay() {
            @Override public List<ItemStack> getStacks(ContextParameterMap parameters) { return stacks; }
        };
    }

    default Ingredient toVanilla() { return new CustomIngredientImpl(this); }
}
