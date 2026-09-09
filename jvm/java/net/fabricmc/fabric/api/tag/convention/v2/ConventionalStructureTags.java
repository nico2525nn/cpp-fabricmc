package net.fabricmc.fabric.api.tag.convention.v2;

import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.util.Identifier;
import net.minecraft.world.gen.structure.Structure;

/** Generated from the Fabric v2 convention-tags 1.21.4 public catalog. */
public final class ConventionalStructureTags {
    private ConventionalStructureTags() { }

    private static TagKey<Structure> register(String path) {
        return TagKey.of(RegistryKeys.STRUCTURE, Identifier.of("c", path));
    }

    public static final TagKey<Structure> HIDDEN_FROM_DISPLAYERS = register("hidden_from_displayers");
    public static final TagKey<Structure> HIDDEN_FROM_LOCATOR_SELECTION = register("hidden_from_locator_selection");
}
