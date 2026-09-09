package net.fabricmc.fabric.impl.recipe.ingredient;

import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Stream;
import net.fabricmc.fabric.api.recipe.v1.ingredient.CustomIngredient;
import net.fabricmc.fabric.api.recipe.v1.ingredient.CustomIngredientSerializer;
import net.minecraft.item.Item;
import net.minecraft.recipe.Ingredient;
import net.minecraft.registry.entry.RegistryEntry;

/** Runtime bridge for Fabric custom ingredients. */
public final class CustomIngredientImpl extends Ingredient
        implements net.fabricmc.fabric.api.recipe.v1.ingredient.FabricIngredient {
    private static final Map<net.minecraft.util.Identifier, CustomIngredientSerializer<?>> SERIALIZERS =
        new ConcurrentHashMap<>();
    private final CustomIngredient customIngredient;

    public CustomIngredientImpl(CustomIngredient customIngredient) {
        super(Ingredient.entryListFromStream(customIngredient == null
            ? Stream.empty() : customIngredient.getMatchingItems()));
        this.customIngredient = Objects.requireNonNull(customIngredient, "customIngredient");
    }

    @Override public boolean test(net.minecraft.item.ItemStack stack) {
        return customIngredient.test(stack);
    }

    @Override public Stream<RegistryEntry<Item>> getMatchingItems() {
        return customIngredient.getMatchingItems();
    }

    @Override public CustomIngredient getCustomIngredient() { return customIngredient; }

    public static void registerSerializer(CustomIngredientSerializer<?> serializer) {
        Objects.requireNonNull(serializer, "serializer");
        Objects.requireNonNull(serializer.getIdentifier(), "serializer identifier");
        CustomIngredientSerializer<?> old = SERIALIZERS.putIfAbsent(serializer.getIdentifier(), serializer);
        if (old != null) throw new IllegalArgumentException("Duplicate custom ingredient serializer: " + serializer.getIdentifier());
    }

    public static CustomIngredientSerializer<?> getSerializer(net.minecraft.util.Identifier identifier) {
        return identifier == null ? null : SERIALIZERS.get(identifier);
    }

    public static void clearSerializers() { SERIALIZERS.clear(); }
}
