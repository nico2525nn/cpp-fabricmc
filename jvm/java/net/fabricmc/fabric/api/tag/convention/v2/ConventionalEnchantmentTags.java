package net.fabricmc.fabric.api.tag.convention.v2;

import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.util.Identifier;
import net.minecraft.enchantment.Enchantment;

/** Generated from the Fabric v2 convention-tags 1.21.4 public catalog. */
public final class ConventionalEnchantmentTags {
    private ConventionalEnchantmentTags() { }

    private static TagKey<Enchantment> register(String path) {
        return TagKey.of(RegistryKeys.ENCHANTMENT, Identifier.of("c", path));
    }

    public static final TagKey<Enchantment> INCREASE_BLOCK_DROPS = register("increase_block_drops");
    public static final TagKey<Enchantment> INCREASE_ENTITY_DROPS = register("increase_entity_drops");
    public static final TagKey<Enchantment> WEAPON_DAMAGE_ENHANCEMENTS = register("weapon_damage_enhancements");
    public static final TagKey<Enchantment> ENTITY_SPEED_ENHANCEMENTS = register("entity_speed_enhancements");
    public static final TagKey<Enchantment> ENTITY_AUXILIARY_MOVEMENT_ENHANCEMENTS = register("entity_auxiliary_movement_enhancements");
    public static final TagKey<Enchantment> ENTITY_DEFENSE_ENHANCEMENTS = register("entity_defense_enhancements");
}
