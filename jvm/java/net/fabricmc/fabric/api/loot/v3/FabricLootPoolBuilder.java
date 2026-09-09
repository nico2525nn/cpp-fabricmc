package net.fabricmc.fabric.api.loot.v3;

import java.util.Collection;
import net.minecraft.loot.LootPool;
import net.minecraft.loot.condition.LootCondition;
import net.minecraft.loot.entry.LootPoolEntry;
import net.minecraft.loot.function.LootFunction;

/** Fabric 1.21.4 extensions for vanilla loot-pool builders. */
public interface FabricLootPoolBuilder {
    default LootPool.Builder with(LootPoolEntry entry) { throw new UnsupportedOperationException("implemented by LootPool.Builder"); }
    default LootPool.Builder with(Collection<? extends LootPoolEntry> entries) { throw new UnsupportedOperationException("implemented by LootPool.Builder"); }
    default LootPool.Builder conditionally(LootCondition condition) { throw new UnsupportedOperationException("implemented by LootPool.Builder"); }
    default LootPool.Builder conditionally(Collection<? extends LootCondition> conditions) { throw new UnsupportedOperationException("implemented by LootPool.Builder"); }
    default LootPool.Builder apply(LootFunction function) { throw new UnsupportedOperationException("implemented by LootPool.Builder"); }
    default LootPool.Builder apply(Collection<? extends LootFunction> functions) { throw new UnsupportedOperationException("implemented by LootPool.Builder"); }
    static LootPool.Builder copyOf(LootPool pool) { return LootPool.Builder.copyOf(pool); }
}
