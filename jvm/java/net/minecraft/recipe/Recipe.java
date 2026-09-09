package net.minecraft.recipe;

import com.mojang.serialization.Codec;
import java.util.List;
import net.minecraft.item.ItemStack;
import net.minecraft.recipe.book.RecipeBookCategory;
import net.minecraft.recipe.display.RecipeDisplay;
import net.minecraft.registry.RegistryWrapper;
import net.minecraft.world.World;

/** Server-side recipe contract for the 1.21.4 shadow runtime. */
public interface Recipe<I extends net.minecraft.recipe.input.RecipeInput> {
    Codec<Recipe<?>> CODEC = new Codec<>() { };
    net.minecraft.network.codec.PacketCodec<net.minecraft.network.RegistryByteBuf, Recipe<?>> PACKET_CODEC =
        net.minecraft.network.codec.PacketCodec.ofLegacy((buffer, recipe) -> { }, buffer -> null);

    RecipeType<? extends Recipe<?>> getType();

    default String getGroup() { return ""; }
    default boolean showNotification() { return true; }
    default List<RecipeDisplay> getDisplays() { return List.of(); }
    boolean matches(I input, World world);
    default boolean isIgnoredInRecipeBook() { return false; }
    default RecipeBookCategory getRecipeBookCategory() { return RecipeBookCategory.CRAFTING; }
    default Object getIngredientPlacement() { return null; }
    ItemStack craft(I input, RegistryWrapper.WrapperLookup registries);
}
