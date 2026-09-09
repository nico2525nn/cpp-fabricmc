package net.minecraft.recipe.input;

import net.minecraft.item.ItemStack;

/**
 * The common 1.21.4 recipe input contract.  Vanilla recipe implementations use
 * this interface instead of exposing a particular inventory implementation.
 */
public interface RecipeInput {
    int size();

    ItemStack getStackInSlot(int slot);

    default boolean isEmpty() {
        for (int slot = 0; slot < size(); slot++) {
            ItemStack stack = getStackInSlot(slot);
            if (stack != null && !stack.isEmpty()) return false;
        }
        return true;
    }
}
