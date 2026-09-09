package net.fabricmc.fabric.api.recipe.v1.ingredient;

import com.mojang.serialization.MapCodec;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.util.Identifier;

/** Codec and identity contract for a custom ingredient. */
public interface CustomIngredientSerializer<T extends CustomIngredient> {
    static void register(CustomIngredientSerializer<?> serializer) {
        net.fabricmc.fabric.impl.recipe.ingredient.CustomIngredientImpl.registerSerializer(serializer);
    }

    static CustomIngredientSerializer<?> get(Identifier identifier) {
        return net.fabricmc.fabric.impl.recipe.ingredient.CustomIngredientImpl.getSerializer(identifier);
    }

    Identifier getIdentifier();
    MapCodec<T> getCodec();
    PacketCodec<RegistryByteBuf, T> getPacketCodec();
}
