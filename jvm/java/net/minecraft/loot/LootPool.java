package net.minecraft.loot;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import net.minecraft.loot.condition.LootCondition;
import net.minecraft.loot.function.LootFunction;
import net.minecraft.loot.provider.number.LootNumberProvider;
import net.minecraft.loot.entry.LootPoolEntry;

/**
 * Mutable-at-build-time loot pool model used by Fabric's server loot events.
 * Native loot generation may supply richer values, but all builder mutations
 * are retained instead of being discarded at the JVM boundary.
 */
public class LootPool {
    private final List<LootPoolEntry> entries;
    private final List<LootCondition> conditions;
    private final List<LootFunction> functions;
    private final LootNumberProvider rolls;
    private final LootNumberProvider bonusRolls;

    public LootPool() {
        this(List.of(), List.of(), List.of(), null, null);
    }

    public LootPool(Collection<? extends LootPoolEntry> entries,
                    Collection<? extends LootCondition> conditions,
                    Collection<? extends LootFunction> functions,
                    LootNumberProvider rolls,
                    LootNumberProvider bonusRolls) {
        this.entries = List.copyOf(entries == null ? List.of() : entries);
        this.conditions = List.copyOf(conditions == null ? List.of() : conditions);
        this.functions = List.copyOf(functions == null ? List.of() : functions);
        this.rolls = rolls;
        this.bonusRolls = bonusRolls;
    }

    public List<LootPoolEntry> getEntries() { return entries; }
    public List<LootCondition> getConditions() { return conditions; }
    public List<LootFunction> getFunctions() { return functions; }
    public LootNumberProvider getRolls() { return rolls; }
    public LootNumberProvider getBonusRolls() { return bonusRolls; }

    public static Builder builder() { return new Builder(); }

    public static class Builder implements
            net.fabricmc.fabric.api.loot.v2.FabricLootPoolBuilder,
            net.fabricmc.fabric.api.loot.v3.FabricLootPoolBuilder {
        private final List<LootPoolEntry> entries = new ArrayList<>();
        private final List<LootCondition> conditions = new ArrayList<>();
        private final List<LootFunction> functions = new ArrayList<>();
        private LootNumberProvider rolls;
        private LootNumberProvider bonusRolls;

        public Builder() { }

        public static Builder copyOf(LootPool pool) {
            Builder builder = new Builder();
            if (pool == null) return builder;
            builder.entries.addAll(pool.getEntries());
            builder.conditions.addAll(pool.getConditions());
            builder.functions.addAll(pool.getFunctions());
            builder.rolls = pool.getRolls();
            builder.bonusRolls = pool.getBonusRolls();
            return builder;
        }

        public Builder rolls(LootNumberProvider value) { rolls = value; return this; }
        public Builder bonusRolls(LootNumberProvider value) { bonusRolls = value; return this; }

        public Builder with(LootPoolEntry.Builder entryBuilder) {
            if (entryBuilder != null) with(entryBuilder.build());
            return this;
        }

        @Override
        public Builder with(LootPoolEntry entry) {
            if (entry != null) entries.add(entry);
            return this;
        }

        @Override
        public Builder with(Collection<? extends LootPoolEntry> values) {
            if (values != null) for (LootPoolEntry entry : values) with(entry);
            return this;
        }

        public Builder conditionally(LootCondition.Builder conditionBuilder) {
            if (conditionBuilder != null) conditionally(conditionBuilder.build());
            return this;
        }

        @Override
        public Builder conditionally(LootCondition condition) {
            if (condition != null) conditions.add(condition);
            return this;
        }

        @Override
        public Builder conditionally(Collection<? extends LootCondition> values) {
            if (values != null) for (LootCondition condition : values) conditionally(condition);
            return this;
        }

        public Builder apply(LootFunction.Builder functionBuilder) {
            if (functionBuilder != null) apply(functionBuilder.build());
            return this;
        }

        @Override
        public Builder apply(LootFunction function) {
            if (function != null) functions.add(function);
            return this;
        }

        @Override
        public Builder apply(Collection<? extends LootFunction> values) {
            if (values != null) for (LootFunction function : values) apply(function);
            return this;
        }

        public LootPool build() {
            return new LootPool(entries, conditions, functions, rolls, bonusRolls);
        }
    }
}
