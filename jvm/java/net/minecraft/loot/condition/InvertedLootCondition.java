package net.minecraft.loot.condition;

import net.minecraft.loot.context.LootContext;

public final class InvertedLootCondition implements LootCondition {
    private final LootCondition term;
    public InvertedLootCondition(LootCondition term) { this.term = term; }
    @Override public boolean test(LootContext context) { return term == null || !term.test(context); }
    public static Builder builder(LootCondition.Builder term) { return new Builder(term); }
    public static final class Builder implements LootCondition.Builder {
        private final LootCondition.Builder term;
        private Builder(LootCondition.Builder term) { this.term = term; }
        @Override public InvertedLootCondition build() { return new InvertedLootCondition(term == null ? null : term.build()); }
    }
}
