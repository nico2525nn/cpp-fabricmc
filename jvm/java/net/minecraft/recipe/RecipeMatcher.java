package net.minecraft.recipe;

import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.stream.Stream;

/**
 * Server-side recipe resource matcher contract used by Ingredient.
 *
 * <p>The native gameplay layer owns the authoritative crafting inventory;
 * this class supplies the small public matching surface that Fabric recipe
 * implementations link against.</p>
 */
public class RecipeMatcher<T> {
    /** A recipe input that can accept one or more registry values. */
    public interface RawIngredient<T> {
        Stream<T> getMatchingItems();
        boolean acceptsItem(T item);
    }

    /** Callback invoked for each resource consumed by a successful match. */
    @FunctionalInterface
    public interface ItemCallback<T> {
        void accept(T item, int count);
    }

    private final java.util.Map<T, Integer> available = new java.util.HashMap<>();

    public RecipeMatcher() { }

    public void add(T input, int count) {
        if (input != null && count > 0) available.merge(input, count, Integer::sum);
    }

    public void clear() { available.clear(); }

    public boolean match(List<? extends RawIngredient<T>> ingredients,
                         int quantity, ItemCallback<T> callback) {
        if (ingredients == null || quantity <= 0) return false;
        java.util.Map<T, Integer> remaining = new java.util.HashMap<>(available);
        for (RawIngredient<T> ingredient : ingredients) {
            if (ingredient == null) return false;
            T selected = ingredient.getMatchingItems()
                .filter(Objects::nonNull)
                .filter(value -> remaining.getOrDefault(value, 0) >= quantity)
                .findFirst().orElse(null);
            if (selected == null) return false;
            remaining.merge(selected, -quantity, Integer::sum);
        }
        if (callback != null) {
            for (RawIngredient<T> ingredient : ingredients) {
                ingredient.getMatchingItems().filter(Objects::nonNull).findFirst()
                    .ifPresent(value -> callback.accept(value, quantity));
            }
        }
        available.clear();
        available.putAll(remaining);
        return true;
    }

    public int countCrafts(List<? extends RawIngredient<T>> ingredients,
                           int max, ItemCallback<T> callback) {
        if (ingredients == null || max <= 0) return 0;
        int count = 0;
        while (count < max && match(ingredients, 1, callback)) count++;
        return count;
    }
}
