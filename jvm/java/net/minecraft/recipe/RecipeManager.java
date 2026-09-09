package net.minecraft.recipe;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Stream;
import net.fabricmc.fabric.api.recipe.v1.FabricServerRecipeManager;
import net.minecraft.recipe.input.RecipeInput;
import net.minecraft.world.World;

/**
 * Small, reload-friendly recipe manager.  The native server may populate this
 * table from its data-pack loader; JVM mods can also use the public registration
 * helpers while a world is being bootstrapped.
 */
public class RecipeManager implements FabricServerRecipeManager {
    private final Map<RecipeType<?>, List<RecipeEntry<?>>> recipes = new LinkedHashMap<>();

    public synchronized <I extends RecipeInput, T extends Recipe<I>> void add(RecipeType<T> type,
                                                                                 RecipeEntry<T> entry) {
        if (type == null || entry == null) return;
        recipes.computeIfAbsent(type, ignored -> new ArrayList<>()).add(entry);
    }

    public synchronized void clear() { recipes.clear(); }

    @Override
    @SuppressWarnings("unchecked")
    public synchronized <I extends RecipeInput, T extends Recipe<I>> Stream<RecipeEntry<T>>
            getAllMatches(RecipeType<T> type, I input, World world) {
        if (type == null || input == null || input.isEmpty()) return Stream.empty();
        return recipes.getOrDefault(type, List.of()).stream()
            .map(entry -> (RecipeEntry<T>) entry)
            .filter(entry -> entry.value().matches(input, world));
    }

    @Override
    @SuppressWarnings("unchecked")
    public synchronized <I extends RecipeInput, T extends Recipe<I>> Collection<RecipeEntry<T>>
            getAllOfType(RecipeType<T> type) {
        if (type == null) return List.of();
        List<RecipeEntry<T>> result = new ArrayList<>();
        for (RecipeEntry<?> entry : recipes.getOrDefault(type, List.of()))
            result.add((RecipeEntry<T>) entry);
        return Collections.unmodifiableList(result);
    }

    public synchronized Collection<RecipeEntry<?>> getAll() {
        List<RecipeEntry<?>> result = new ArrayList<>();
        recipes.values().forEach(result::addAll);
        return Collections.unmodifiableList(result);
    }
}
