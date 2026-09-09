package net.minecraft.loot.condition;

/** Registry descriptor for a loot condition implementation. */
public final class LootConditionType {
    private final LootCondition condition;
    public LootConditionType() { this(null); }
    public LootConditionType(LootCondition condition) { this.condition = condition; }
    public LootCondition condition() { return condition; }
}
