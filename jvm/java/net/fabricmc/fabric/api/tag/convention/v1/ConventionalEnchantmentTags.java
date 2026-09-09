package net.fabricmc.fabric.api.tag.convention.v1;

import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.util.Identifier;
import net.minecraft.enchantment.Enchantment;

/** Generated from the Fabric v1 convention-tags 1.21.4 public catalog. */
public final class ConventionalEnchantmentTags {
    private ConventionalEnchantmentTags() { }

    private static TagKey<Enchantment> register(String path) {
        return TagKey.of(RegistryKeys.ENCHANTMENT, Identifier.of("c", path));
    }

    public static final TagKey<Enchantment> INCREASES_BLOCK_DROPS = register("fortune");
    public static final TagKey<Enchantment> INCREASES_ENTITY_DROPS = register("looting");
    public static final TagKey<Enchantment> WEAPON_DAMAGE_ENHANCEMENT = register("weapon_damage_enhancement");
    public static final TagKey<Enchantment> ENTITY_MOVEMENT_ENHANCEMENT = register("entity_movement_enhancement");
    public static final TagKey<Enchantment> ENTITY_DEFENSE_ENHANCEMENT = register("entity_defense_enhancement");
}
