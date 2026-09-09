package net.minecraft.recipe;

import java.util.Objects;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.registry.RegistryKey;

/** Pair of a registry key and a recipe value. */
public record RecipeEntry<T extends Recipe<?>>(RegistryKey<Recipe<?>> id, T value) {
    public static final PacketCodec<RegistryByteBuf, RecipeEntry<?>> PACKET_CODEC =
        PacketCodec.ofLegacy((buffer, entry) -> { }, buffer -> null);

    public RecipeEntry {
        Objects.requireNonNull(id, "id");
        Objects.requireNonNull(value, "value");
    }
}
