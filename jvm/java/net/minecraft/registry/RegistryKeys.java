package net.minecraft.registry;

import net.minecraft.world.World;
import net.minecraft.item.Item;
import net.minecraft.block.Block;
import net.minecraft.entity.EntityType;
import net.minecraft.fluid.Fluid;
import net.minecraft.util.Identifier;

public final class RegistryKeys {
    private RegistryKeys() {}
    public static final RegistryKey<Registry<?>> ROOT = RegistryKey.ofRegistry(Identifier.of("minecraft", "root"));
    public static final RegistryKey<Registry<Block>> BLOCK = RegistryKey.of( ROOT, Identifier.of("minecraft", "block"));
    public static final RegistryKey<Registry<Item>> ITEM = RegistryKey.of( ROOT, Identifier.of("minecraft", "item"));
    public static final RegistryKey<Registry<EntityType<?>>> ENTITY_TYPE = RegistryKey.of(ROOT, Identifier.of("minecraft", "entity_type"));
    public static final RegistryKey<Registry<Fluid>> FLUID = RegistryKey.of(ROOT, Identifier.of("minecraft", "fluid"));
    public static final RegistryKey<Registry<Object>> ITEM_GROUP = RegistryKey.of(ROOT, Identifier.of("minecraft", "item_group"));
    public static final RegistryKey<Registry<World>> WORLD = RegistryKey.of(ROOT, Identifier.of("minecraft", "world"));
    public static final RegistryKey<Registry<Object>> BIOME = RegistryKey.of(ROOT, Identifier.of("minecraft", "worldgen/biome"));
    public static final RegistryKey<Registry<Object>> SOUND_EVENT = RegistryKey.of(ROOT, Identifier.of("minecraft", "sound_event"));
}
