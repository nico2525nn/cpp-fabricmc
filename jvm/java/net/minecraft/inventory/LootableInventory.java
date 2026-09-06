package net.minecraft.inventory;

import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.registry.RegistryKey;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.random.Random;
import net.minecraft.world.BlockView;

/** 1.21.4 loot-table-backed inventory contract. */
public interface LootableInventory extends Inventory {
    String LOOT_TABLE_KEY = "LootTable";
    String LOOT_TABLE_SEED_KEY = "LootTableSeed";

    default void setLootTable(RegistryKey<?> lootTable) { }
    default RegistryKey<?> getLootTable() { return null; }
    default void setLootTable(RegistryKey<?> lootTable, long seed) { setLootTable(lootTable); }
    default long getLootTableSeed() { return 0L; }
    default boolean writeLootTable(NbtCompound nbt) { return false; }
    default boolean readLootTable(NbtCompound nbt) { return false; }
    default void generateLoot(PlayerEntity player) { }

    static void setLootTable(BlockView world, Random random, BlockPos pos, RegistryKey<?> lootTable) { }
}
