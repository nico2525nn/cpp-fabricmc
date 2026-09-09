package net.minecraft.recipe;

import net.minecraft.registry.Registry;
import net.minecraft.registry.Registries;
import net.minecraft.util.Identifier;

/** Type discriminator used to partition the server recipe manager. */
public interface RecipeType<T extends Recipe<?>> {
    RecipeType<Recipe<?>> CRAFTING = register("crafting");
    RecipeType<Recipe<?>> SMELTING = register("smelting");
    RecipeType<Recipe<?>> BLASTING = register("blasting");
    RecipeType<Recipe<?>> SMOKING = register("smoking");
    RecipeType<Recipe<?>> CAMPFIRE_COOKING = register("campfire_cooking");
    RecipeType<Recipe<?>> STONECUTTING = register("stonecutting");
    RecipeType<Recipe<?>> SMITHING = register("smithing");

    static <T extends Recipe<?>> RecipeType<T> register(String id) {
        RecipeType<T> type = new RecipeType<>() {
            @Override public String toString() { return "RecipeType[" + id + "]"; }
        };
        if (id != null) {
            @SuppressWarnings("unchecked") Registry<RecipeType<?>> registry =
                (Registry<RecipeType<?>>) (Registry<?>) Registries.RECIPE_TYPE;
            Identifier identifier = Identifier.of("minecraft", id);
            if (!registry.containsId(identifier)) registry.registerValue(identifier, type);
        }
        return type;
    }
}
