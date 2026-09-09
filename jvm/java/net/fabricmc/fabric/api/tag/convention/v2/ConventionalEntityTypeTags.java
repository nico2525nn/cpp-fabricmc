package net.fabricmc.fabric.api.tag.convention.v2;

import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.util.Identifier;
import net.minecraft.entity.EntityType;

/** Generated from the Fabric v2 convention-tags 1.21.4 public catalog. */
public final class ConventionalEntityTypeTags {
    private ConventionalEntityTypeTags() { }

    public static final TagKey<EntityType<?>> BOSSES = register("bosses");
    public static final TagKey<EntityType<?>> MINECARTS = register("minecarts");
    public static final TagKey<EntityType<?>> BOATS = register("boats");
    public static final TagKey<EntityType<?>> CAPTURING_NOT_SUPPORTED = register("capturing_not_supported");
    public static final TagKey<EntityType<?>> TELEPORTING_NOT_SUPPORTED = register("teleporting_not_supported");

    private static TagKey<EntityType<?>> register(String path) {
        return TagKey.of(RegistryKeys.ENTITY_TYPE, Identifier.of("c", path));
    }

}
