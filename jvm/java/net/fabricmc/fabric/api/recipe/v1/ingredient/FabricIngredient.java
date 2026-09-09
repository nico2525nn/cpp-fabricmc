package net.fabricmc.fabric.api.recipe.v1.ingredient;


/** Extensions injected into vanilla {@link net.minecraft.recipe.Ingredient}. */
public interface FabricIngredient {
    default CustomIngredient getCustomIngredient() { return null; }

    default boolean requiresTesting() {
        CustomIngredient custom = getCustomIngredient();
        return custom != null && custom.requiresTesting();
    }
}
