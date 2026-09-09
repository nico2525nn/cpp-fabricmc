package net.fabricmc.fabric.api.tag.convention.v1;

import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.util.Identifier;
import net.minecraft.fluid.Fluid;

/** Generated from the Fabric v1 convention-tags 1.21.4 public catalog. */
public final class ConventionalFluidTags {
    private ConventionalFluidTags() { }

    private static TagKey<Fluid> register(String path) {
        return TagKey.of(RegistryKeys.FLUID, Identifier.of("c", path));
    }

    public static final TagKey<Fluid> LAVA = register("lava");
    public static final TagKey<Fluid> WATER = register("water");
    public static final TagKey<Fluid> MILK = register("milk");
    public static final TagKey<Fluid> HONEY = register("honey");
}
