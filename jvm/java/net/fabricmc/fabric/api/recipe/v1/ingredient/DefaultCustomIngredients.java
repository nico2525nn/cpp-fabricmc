package net.fabricmc.fabric.api.recipe.v1.ingredient;

import java.util.Arrays;
import java.util.List;
import java.util.Objects;
import java.util.function.Predicate;
import java.util.function.UnaryOperator;
import java.util.stream.Stream;
import net.minecraft.component.ComponentChanges;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.recipe.Ingredient;
import net.minecraft.registry.entry.RegistryEntry;

/** Built-in composite custom ingredients shipped by Fabric API. */
public final class DefaultCustomIngredients {
    private DefaultCustomIngredients() { }

    public static Ingredient all(Ingredient... ingredients) {
        Ingredient[] values = requireIngredients(ingredients);
        return custom(stack -> Arrays.stream(values).allMatch(value -> value.test(stack)), values);
    }

    public static Ingredient any(Ingredient... ingredients) {
        Ingredient[] values = requireIngredients(ingredients);
        return custom(stack -> Arrays.stream(values).anyMatch(value -> value.test(stack)), values);
    }

    public static Ingredient difference(Ingredient base, Ingredient subtracted) {
        Objects.requireNonNull(base, "base");
        Objects.requireNonNull(subtracted, "subtracted");
        return custom(stack -> base.test(stack) && !subtracted.test(stack), base, subtracted);
    }

    public static Ingredient components(Ingredient base, ComponentChanges changes) {
        Objects.requireNonNull(base, "base");
        Objects.requireNonNull(changes, "changes");
        return custom(stack -> base.test(stack) && matchesChanges(stack, changes), base);
    }

    public static Ingredient components(Ingredient base,
                                        UnaryOperator<ComponentChanges.Builder> operator) {
        Objects.requireNonNull(operator, "operator");
        return components(base, operator.apply(ComponentChanges.builder()).build());
    }

    public static Ingredient components(ItemStack stack) {
        Objects.requireNonNull(stack, "stack");
        return components(Ingredient.ofItem(stack.getItem()), stack.getComponentChanges());
    }

    public static Ingredient customData(Ingredient base, NbtCompound nbt) {
        Objects.requireNonNull(base, "base");
        if (nbt == null || nbt.isEmpty()) throw new IllegalArgumentException("NBT must not be empty");
        return custom(stack -> base.test(stack) && containsNbt(stack.getNbt(), nbt), base);
    }

    private static Ingredient[] requireIngredients(Ingredient[] ingredients) {
        if (ingredients == null || ingredients.length == 0) throw new IllegalArgumentException("ingredients must not be empty");
        Ingredient[] copy = ingredients.clone();
        for (Ingredient ingredient : copy) Objects.requireNonNull(ingredient, "ingredient");
        return copy;
    }

    private static Ingredient custom(Predicate<ItemStack> predicate, Ingredient... displaySources) {
        List<RegistryEntry<Item>> display = Arrays.stream(displaySources)
            .flatMap(ingredient -> ingredient.getMatchingItems())
            .filter(Objects::nonNull).distinct().toList();
        CustomIngredient value = new CustomIngredient() {
            @Override public boolean test(ItemStack stack) { return stack != null && predicate.test(stack); }
            @Override public Stream<RegistryEntry<Item>> getMatchingItems() { return display.stream(); }
            @Override public boolean requiresTesting() { return true; }
            @Override public CustomIngredientSerializer<?> getSerializer() { return null; }
        };
        return value.toVanilla();
    }

    private static boolean matchesChanges(ItemStack stack, ComponentChanges changes) {
        if (stack == null) return false;
        for (var entry : changes.entrySet()) {
            var actual = stack.get(entry.getKey());
            if (entry.getValue().isPresent()) {
                if (!Objects.equals(actual, entry.getValue().get())) return false;
            } else if (actual != null || stack.contains(entry.getKey())) {
                return false;
            }
        }
        return true;
    }

    private static boolean containsNbt(NbtCompound actual, NbtCompound expected) {
        if (actual == null || expected == null) return false;
        for (String key : expected.getKeys()) {
            if (!actual.contains(key)) return false;
            if (!Objects.equals(actual.get(key), expected.get(key))) return false;
        }
        return true;
    }
}
