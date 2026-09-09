package net.minecraft.loot.entry;

/** Registry descriptor for a loot-pool entry implementation. */
public final class LootPoolEntryType {
    private final LootPoolEntry entry;
    public LootPoolEntryType() { this(null); }
    public LootPoolEntryType(LootPoolEntry entry) { this.entry = entry; }
    public LootPoolEntry entry() { return entry; }
}
