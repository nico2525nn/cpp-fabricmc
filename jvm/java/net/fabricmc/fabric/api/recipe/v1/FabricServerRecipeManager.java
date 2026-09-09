package net.fabricmc.fabric.api.recipe.v1;

import java.util.Collection;
import java.util.stream.Stream;
import net.minecraft.recipe.Recipe;
import net.minecraft.recipe.RecipeEntry;
import net.minecraft.recipe.RecipeType;
import net.minecraft.recipe.input.RecipeInput;
import net.minecraft.world.World;

/** Public Fabric extensions exposed by the server recipe manager. */
public interface FabricServerRecipeManager {
    default <I extends RecipeInput, T extends Recipe<I>> Stream<RecipeEntry<T>>
            getAllMatches(RecipeType<T> type, I input, World world) {
        return Stream.empty();
    }

    default <I extends RecipeInput, T extends Recipe<I>> Collection<RecipeEntry<T>>
            getAllOfType(RecipeType<T> type) {
        return java.util.List.of();
    }
}
