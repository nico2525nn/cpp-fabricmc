package net.fabricmc.fabric.api.resource.conditions.v1;

import com.mojang.serialization.Codec;
import com.mojang.serialization.MapCodec;
import java.util.Objects;
import net.minecraft.util.Identifier;

/** Identifier and serializer pair for a resource condition. */
public interface ResourceConditionType<T extends ResourceCondition> {
    Codec<ResourceConditionType<?>> TYPE_CODEC = new Codec<>() { };

    Identifier id();
    MapCodec<T> codec();

    static <T extends ResourceCondition> ResourceConditionType<T> create(
            Identifier id, MapCodec<T> codec) {
        Objects.requireNonNull(id, "id");
        Objects.requireNonNull(codec, "codec");
        return new ResourceConditionType<>() {
            @Override public Identifier id() { return id; }
            @Override public MapCodec<T> codec() { return codec; }
            @Override public String toString() { return id.toString(); }
        };
    }
}
