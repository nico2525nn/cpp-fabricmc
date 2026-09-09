package net.fabricmc.fabric.api.loot.v2;

import java.util.Collection;
import java.util.function.Consumer;
import net.minecraft.loot.LootPool;
import net.minecraft.loot.LootTable;
import net.minecraft.loot.function.LootFunction;

/** Convenience operations mixed into the vanilla loot-table builder. */
public interface FabricLootTableBuilder {
    default LootTable.Builder pool(LootPool pool) { throw new UnsupportedOperationException("implemented by LootTable.Builder"); }
    default LootTable.Builder apply(LootFunction function) { throw new UnsupportedOperationException("implemented by LootTable.Builder"); }
    default LootTable.Builder pools(Collection<? extends LootPool> pools) { throw new UnsupportedOperationException("implemented by LootTable.Builder"); }
    default LootTable.Builder apply(Collection<? extends LootFunction> functions) { throw new UnsupportedOperationException("implemented by LootTable.Builder"); }
    default LootTable.Builder modifyPools(Consumer<? super LootPool.Builder> modifier) { throw new UnsupportedOperationException("implemented by LootTable.Builder"); }
    static LootTable.Builder copyOf(LootTable table) {
        LootTable.Builder builder = LootTable.builder().type(table == null ? null : table.getType());
        if (table == null) return builder;
        builder.pools(table.getPools()).apply(table.getFunctions());
        table.getRandomSequenceId().ifPresent(builder::randomSequenceId);
        return builder;
    }
}
