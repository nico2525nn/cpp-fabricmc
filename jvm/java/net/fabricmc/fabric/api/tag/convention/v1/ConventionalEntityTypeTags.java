package net.fabricmc.fabric.api.tag.convention.v1;

import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.util.Identifier;
import net.minecraft.entity.EntityType;

/** Generated from the Fabric v1 convention-tags 1.21.4 public catalog. */
public final class ConventionalEntityTypeTags {
    private ConventionalEntityTypeTags() { }

    public static final TagKey<EntityType<?>> BOSSES = register("bosses");
    public static final TagKey<EntityType<?>> MINECARTS = register("minecarts");
    public static final TagKey<EntityType<?>> BOATS = register("boats");

    private static TagKey<EntityType<?>> register(String path) {
        return TagKey.of(RegistryKeys.ENTITY_TYPE, Identifier.of("c", path));
    }

}
