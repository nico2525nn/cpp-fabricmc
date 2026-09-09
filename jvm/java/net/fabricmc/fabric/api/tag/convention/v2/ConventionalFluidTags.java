package net.fabricmc.fabric.api.tag.convention.v2;

import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.util.Identifier;
import net.minecraft.fluid.Fluid;

/** Generated from the Fabric v2 convention-tags 1.21.4 public catalog. */
public final class ConventionalFluidTags {
    private ConventionalFluidTags() { }

    private static TagKey<Fluid> register(String path) {
        return TagKey.of(RegistryKeys.FLUID, Identifier.of("c", path));
    }

    public static final TagKey<Fluid> LAVA = register("lava");
    public static final TagKey<Fluid> WATER = register("water");
    public static final TagKey<Fluid> MILK = register("milk");
    public static final TagKey<Fluid> HONEY = register("honey");
    public static final TagKey<Fluid> GASEOUS = register("gaseous");
    public static final TagKey<Fluid> EXPERIENCE = register("experience");
    public static final TagKey<Fluid> POTION = register("potion");
    public static final TagKey<Fluid> SUSPICIOUS_STEW = register("suspicious_stew");
    public static final TagKey<Fluid> MUSHROOM_STEW = register("mushroom_stew");
    public static final TagKey<Fluid> RABBIT_STEW = register("rabbit_stew");
    public static final TagKey<Fluid> BEETROOT_SOUP = register("beetroot_soup");
    public static final TagKey<Fluid> HIDDEN_FROM_RECIPE_VIEWERS = register("hidden_from_recipe_viewers");
}
