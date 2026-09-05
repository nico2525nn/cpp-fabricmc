package net.minecraft.registry;

import net.minecraft.block.Block;
import net.minecraft.block.Blocks;
import net.minecraft.item.Item;
import net.minecraft.item.Items;
import net.minecraft.util.Identifier;

/** Process-local registry views backed by Java identity, not native IDs. */
public final class Registries {
    private Registries() {}
    public static final Registry<Block> BLOCK = new Registry<>();
    public static final Registry<Item> ITEM = new Registry<>();

    static {
        Registry.register(BLOCK, Identifier.of("minecraft", "air"), Blocks.AIR);
        Registry.register(ITEM, Identifier.of("minecraft", "air"), Items.AIR);
    }
}
