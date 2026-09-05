package net.minecraft.registry;

import net.minecraft.block.Block;
import net.minecraft.block.Blocks;
import net.minecraft.item.Item;
import net.minecraft.item.Items;
import net.minecraft.util.Identifier;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityType;
import net.minecraft.fluid.Fluid;
import net.minecraft.fluid.Fluids;

/** Process-local registry views backed by Java identity, not native IDs. */
public final class Registries {
    private Registries() {}
    public static final Registry<Block> BLOCK = new Registry<>(RegistryKeys.BLOCK);
    public static final Registry<Item> ITEM = new Registry<>(RegistryKeys.ITEM);
    public static final Registry<EntityType<?>> ENTITY_TYPE = new Registry<>(RegistryKeys.ENTITY_TYPE);
    public static final Registry<Fluid> FLUID = new Registry<>(RegistryKeys.FLUID);
    public static final Registry<Registry<?>> ROOT = new Registry<>(RegistryKeys.ROOT);

    static {
        registerIfAbsent(BLOCK, Identifier.of("minecraft", "air"), Blocks.AIR);
        registerIfAbsent(ITEM, Identifier.of("minecraft", "air"), Items.AIR);
        for (Block block : Blocks.values()) registerIfAbsent(BLOCK, block.getId(), block);
        for (Item item : Items.values()) registerIfAbsent(ITEM, item.getId(), item);
        registerIfAbsent(ENTITY_TYPE, Identifier.of("minecraft", "player"), EntityType.PLAYER);
        registerIfAbsent(FLUID, Identifier.of("minecraft", "empty"), Fluids.EMPTY);
        registerIfAbsent(FLUID, Identifier.of("minecraft", "water"), Fluids.WATER);
        registerIfAbsent(FLUID, Identifier.of("minecraft", "lava"), Fluids.LAVA);
        registerIfAbsent(ROOT, RegistryKeys.BLOCK.getValue(), BLOCK);
        registerIfAbsent(ROOT, RegistryKeys.ITEM.getValue(), ITEM);
        registerIfAbsent(ROOT, RegistryKeys.ENTITY_TYPE.getValue(), ENTITY_TYPE);
        registerIfAbsent(ROOT, RegistryKeys.FLUID.getValue(), FLUID);
    }
    private static <T> void registerIfAbsent(Registry<T> registry, Identifier id, T value) {
        if (!registry.containsId(id)) Registry.register(registry, id, value);
    }
    public static Registry<?> byKey(RegistryKey<?> key) {
        if (RegistryKeys.BLOCK.equals(key)) return BLOCK;
        if (RegistryKeys.ITEM.equals(key)) return ITEM;
        if (RegistryKeys.ENTITY_TYPE.equals(key)) return ENTITY_TYPE;
        if (RegistryKeys.FLUID.equals(key)) return FLUID;
        if (RegistryKeys.ROOT.equals(key)) return ROOT;
        if (key != null) {
            Registry<?> registered = ROOT.get(key.getValue());
            if (registered != null) return registered;
        }
        return null;
    }
}
