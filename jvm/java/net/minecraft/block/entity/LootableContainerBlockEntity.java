package net.minecraft.block.entity;

import net.minecraft.block.BlockState;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.inventory.Inventory;
import net.minecraft.inventory.LootableInventory;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.registry.RegistryKey;
import net.minecraft.util.math.BlockPos;

/** Minimal lootable container base used by container and Carpet mixins. */
public abstract class LootableContainerBlockEntity extends BlockEntity implements LootableInventory {
    protected RegistryKey<?> lootTable;
    protected long lootTableSeed;
    private final Inventory inventory = new net.minecraft.inventory.SimpleInventory(27);

    protected LootableContainerBlockEntity(BlockEntityType<?> type, BlockPos pos, BlockState state) { super(type, pos, state); }
    protected LootableContainerBlockEntity() { super(); }
    @Override public int size() { return inventory.size(); }
    @Override public ItemStack getStack(int slot) { return inventory.getStack(slot); }
    @Override public void setStack(int slot, ItemStack stack) { inventory.setStack(slot, stack); }
    @Override public void setLootTable(RegistryKey<?> value) { lootTable = value; }
    @Override public RegistryKey<?> getLootTable() { return lootTable; }
    @Override public void setLootTable(RegistryKey<?> value, long seed) { lootTable = value; lootTableSeed = seed; }
    @Override public long getLootTableSeed() { return lootTableSeed; }
    @Override public boolean writeLootTable(NbtCompound nbt) { return lootTable != null; }
    @Override public boolean readLootTable(NbtCompound nbt) { return false; }
    @Override public void generateLoot(PlayerEntity player) { lootTable = null; lootTableSeed = 0L; }
}
