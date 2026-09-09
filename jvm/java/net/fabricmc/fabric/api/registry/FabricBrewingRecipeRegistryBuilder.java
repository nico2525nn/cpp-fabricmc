package net.fabricmc.fabric.api.registry;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.item.Item;
import net.minecraft.potion.Potion;
import net.minecraft.recipe.BrewingRecipeRegistry;
import net.minecraft.recipe.Ingredient;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.resource.featuretoggle.FeatureSet;

/** Fabric extension point for vanilla brewing recipe construction. */
public interface FabricBrewingRecipeRegistryBuilder {
    Event<BuildCallback> BUILD = EventFactory.createArrayBacked(BuildCallback.class,
        callbacks -> builder -> {
            for (BuildCallback callback : callbacks) callback.build(builder);
        });

    default void registerItemRecipe(Item input, Ingredient ingredient, Item output) {
        if (this instanceof BrewingRecipeRegistry.Builder builder)
            builder.registerItemRecipe(input, ingredient, output);
        else throw new AssertionError("Brewing builder must implement the vanilla builder contract");
    }

    default void registerPotionRecipe(RegistryEntry<Potion> input, Ingredient ingredient,
                                      RegistryEntry<Potion> output) {
        if (this instanceof BrewingRecipeRegistry.Builder builder)
            builder.registerPotionRecipe(input, ingredient, output);
        else throw new AssertionError("Brewing builder must implement the vanilla builder contract");
    }

    default void registerRecipes(Ingredient ingredient, RegistryEntry<Potion> potion) {
        if (this instanceof BrewingRecipeRegistry.Builder builder)
            builder.registerRecipes(ingredient, potion);
        else throw new AssertionError("Brewing builder must implement the vanilla builder contract");
    }

    default FeatureSet getEnabledFeatures() {
        return this instanceof BrewingRecipeRegistry.Builder builder
            ? builder.getEnabledFeatures() : FeatureSet.EMPTY;
    }

    @FunctionalInterface
    interface BuildCallback { void build(BrewingRecipeRegistry.Builder builder); }
}
