package net.minecraft.registry;

import net.minecraft.block.Block;
import net.minecraft.block.Blocks;
import net.minecraft.item.Item;
import net.minecraft.item.Items;
import net.minecraft.util.Identifier;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityType;

/** Process-local registry views backed by Java identity, not native IDs. */
public final class Registries {
    private Registries() {}
    public static final Registry<Block> BLOCK = new Registry<>(RegistryKeys.BLOCK);
    public static final Registry<Item> ITEM = new Registry<>(RegistryKeys.ITEM);
    public static final Registry<EntityType<?>> ENTITY_TYPE = new Registry<>(RegistryKeys.ENTITY_TYPE);
    public static final Registry<Registry<?>> ROOT = new Registry<>(RegistryKeys.ROOT);

    static {
        registerIfAbsent(BLOCK, Identifier.of("minecraft", "air"), Blocks.AIR);
        registerIfAbsent(ITEM, Identifier.of("minecraft", "air"), Items.AIR);
        for (Block block : Blocks.values()) registerIfAbsent(BLOCK, block.getId(), block);
        for (Item item : Items.values()) registerIfAbsent(ITEM, item.getId(), item);
        registerIfAbsent(ENTITY_TYPE, Identifier.of("minecraft", "player"), EntityType.PLAYER);
    }
    private static <T> void registerIfAbsent(Registry<T> registry, Identifier id, T value) {
        if (!registry.containsId(id)) Registry.register(registry, id, value);
    }
    public static Registry<?> byKey(RegistryKey<?> key) {
        if (RegistryKeys.BLOCK.equals(key)) return BLOCK;
        if (RegistryKeys.ITEM.equals(key)) return ITEM;
        if (RegistryKeys.ENTITY_TYPE.equals(key)) return ENTITY_TYPE;
        return null;
    }
}
