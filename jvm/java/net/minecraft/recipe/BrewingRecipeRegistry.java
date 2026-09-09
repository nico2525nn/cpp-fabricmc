package net.minecraft.recipe;

import java.util.ArrayList;
import java.util.List;
import net.fabricmc.fabric.api.registry.FabricBrewingRecipeRegistryBuilder;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.resource.featuretoggle.FeatureSet;
import net.minecraft.potion.Potion;

/** Stateful brewing recipe table with the vanilla/Fabric builder boundary. */
public class BrewingRecipeRegistry {
    public static final BrewingRecipeRegistry EMPTY = new BrewingRecipeRegistry(List.of(), List.of(), List.of());
    private final List<RegistryEntry<Potion>> potionTypes;
    private final List<Recipe> potionRecipes;
    private final List<Recipe> itemRecipes;

    public BrewingRecipeRegistry(List<RegistryEntry<Potion>> potionTypes,
                                 List<Recipe> potionRecipes, List<Recipe> itemRecipes) {
        this.potionTypes = List.copyOf(potionTypes == null ? List.of() : potionTypes);
        this.potionRecipes = List.copyOf(potionRecipes == null ? List.of() : potionRecipes);
        this.itemRecipes = List.copyOf(itemRecipes == null ? List.of() : itemRecipes);
    }

    public static BrewingRecipeRegistry create(FeatureSet enabledFeatures) {
        Builder builder = new Builder(enabledFeatures);
        FabricBrewingRecipeRegistryBuilder.BUILD.invoker().build(builder);
        return builder.build();
    }
    public static BrewingRecipeRegistry createDefault(FeatureSet enabledFeatures) { return create(enabledFeatures); }
    public boolean isBrewable(RegistryEntry<Potion> potion) {
        return potion != null && potionRecipes.stream().anyMatch(recipe -> recipe.to == potion || recipe.to.equals(potion));
    }
    public boolean isPotionType(ItemStack stack) { return containsItem(potionTypes, stack); }
    public boolean isValidIngredient(ItemStack stack) { return isPotionRecipeIngredient(stack) || isItemRecipeIngredient(stack); }
    public boolean isPotionRecipeIngredient(ItemStack stack) {
        return potionRecipes.stream().anyMatch(recipe -> recipe.ingredient.test(stack));
    }
    public boolean isItemRecipeIngredient(ItemStack stack) {
        return itemRecipes.stream().anyMatch(recipe -> recipe.ingredient.test(stack));
    }
    public boolean hasRecipe(ItemStack input, ItemStack ingredient) {
        return hasPotionRecipe(input, ingredient) || hasItemRecipe(input, ingredient);
    }
    public boolean hasPotionRecipe(ItemStack input, ItemStack ingredient) {
        RegistryEntry<Potion> potion = potionFor(input);
        return potion != null && potionRecipes.stream().anyMatch(recipe -> samePotion(recipe.from, potion) && recipe.ingredient.test(ingredient));
    }
    public boolean hasItemRecipe(ItemStack input, ItemStack ingredient) {
        return itemRecipes.stream().anyMatch(recipe -> recipe.fromItem == (input == null ? null : input.getItem()) && recipe.ingredient.test(ingredient));
    }
    public ItemStack craft(ItemStack ingredient, ItemStack input) {
        if (input == null) return ItemStack.EMPTY;
        for (Recipe recipe : itemRecipes)
            if (recipe.fromItem == input.getItem() && recipe.ingredient.test(ingredient)) return new ItemStack(recipe.toItem);
        RegistryEntry<Potion> potion = potionFor(input);
        if (potion != null) for (Recipe recipe : potionRecipes)
            if (samePotion(recipe.from, potion) && recipe.ingredient.test(ingredient)) return new ItemStack(input.getItem());
        return ItemStack.EMPTY;
    }
    private boolean containsItem(List<RegistryEntry<Potion>> values, ItemStack stack) {
        return stack != null && values.stream().anyMatch(value -> value != null && value.value() != null && value.value().toString().equals(stack.getItem().toString()));
    }
    private RegistryEntry<Potion> potionFor(ItemStack stack) { return null; }
    private static boolean samePotion(RegistryEntry<Potion> left, RegistryEntry<Potion> right) { return left == right || (left != null && left.equals(right)); }

    public static final class Recipe {
        private final RegistryEntry<Potion> from;
        private final Ingredient ingredient;
        private final RegistryEntry<Potion> to;
        private final Item fromItem;
        private final Item toItem;
        public Recipe(RegistryEntry<Potion> from, Ingredient ingredient, RegistryEntry<Potion> to) {
            this.from = from; this.ingredient = ingredient == null ? Ingredient.ofItems(new Item[0]) : ingredient; this.to = to;
            this.fromItem = null; this.toItem = null;
        }
        public Recipe(Item from, Ingredient ingredient, Item to) {
            this.from = null; this.ingredient = ingredient == null ? Ingredient.ofItems(new Item[0]) : ingredient; this.to = null;
            this.fromItem = from; this.toItem = to;
        }
        public RegistryEntry<Potion> from() { return from; }
        public Ingredient ingredient() { return ingredient; }
        public RegistryEntry<Potion> to() { return to; }
    }

    public static final class Builder implements FabricBrewingRecipeRegistryBuilder {
        private final FeatureSet enabledFeatures;
        private final List<RegistryEntry<Potion>> potionTypes = new ArrayList<>();
        private final List<Recipe> potionRecipes = new ArrayList<>();
        private final List<Recipe> itemRecipes = new ArrayList<>();
        public Builder(FeatureSet enabledFeatures) { this.enabledFeatures = enabledFeatures == null ? FeatureSet.EMPTY : enabledFeatures; }
        public void registerPotionType(Item item) { }
        public void registerPotionRecipe(RegistryEntry<Potion> input, Item ingredient, RegistryEntry<Potion> output) {
            registerPotionRecipe(input, Ingredient.ofItem(ingredient), output);
        }
        public void registerPotionRecipe(RegistryEntry<Potion> input, Ingredient ingredient, RegistryEntry<Potion> output) {
            if (input != null && ingredient != null && output != null) potionRecipes.add(new Recipe(input, ingredient, output));
        }
        public void registerItemRecipe(Item input, Item ingredient, Item output) { registerItemRecipe(input, Ingredient.ofItem(ingredient), output); }
        public void registerItemRecipe(Item input, Ingredient ingredient, Item output) {
            if (input != null && ingredient != null && output != null) itemRecipes.add(new Recipe(input, ingredient, output));
        }
        public void registerRecipes(Item ingredient, RegistryEntry<Potion> potion) { registerRecipes(Ingredient.ofItem(ingredient), potion); }
        public void registerRecipes(Ingredient ingredient, RegistryEntry<Potion> potion) {
            if (ingredient != null && potion != null) potionRecipes.add(new Recipe(null, ingredient, potion));
        }
        @Override public FeatureSet getEnabledFeatures() { return enabledFeatures; }
        public BrewingRecipeRegistry build() { return new BrewingRecipeRegistry(potionTypes, potionRecipes, itemRecipes); }
    }
}
