package cppfm.corpus.fixture06;

import cppfm.bridge.NativeBridge;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.minecraft.block.AbstractBlock;
import net.minecraft.block.Block;
import net.minecraft.item.Item;
import net.minecraft.registry.Registry;
import net.minecraft.registry.Registries;
import net.minecraft.util.Identifier;

/** Corpus 06: registry identity and reverse lookup. */
public final class RegistryApi implements DedicatedServerModInitializer {
    private static void result(String phase, boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=06 status=" + (ok ? "PASS" : "FAIL") + " phase=" + phase);
    }

    @Override
    public void onInitializeServer() {
        Block block = new Block(AbstractBlock.Settings.create());
        Identifier blockId = Identifier.of("corpus06", "registered_block");
        Block returnedBlock = Registry.register(Registries.BLOCK, blockId, block);
        Item item = new Item(new Item.Settings());
        Identifier itemId = Identifier.of("corpus06", "registered_item");
        Item returnedItem = Registry.register(Registries.ITEM, itemId, item);
        boolean ok = returnedBlock == block && returnedItem == item
            && Registries.BLOCK.containsId(blockId)
            && Registries.BLOCK.get(blockId) == block
            && blockId.equals(Registries.BLOCK.getId(block))
            && Registries.ITEM.get(itemId) == item
            && itemId.equals(Registries.ITEM.getId(item));
        result("registry-identity", ok);
    }
}
