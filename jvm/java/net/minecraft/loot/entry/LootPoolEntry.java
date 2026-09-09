package net.minecraft.loot.entry;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import net.minecraft.loot.condition.LootCondition;
import net.minecraft.loot.entry.LootPoolEntryType;
import net.minecraft.loot.context.LootContext;

/** Base entry retained by loot-pool builders and custom Fabric loot code. */
public class LootPoolEntry {
    private final List<LootCondition> conditions;

    public LootPoolEntry() { this(List.of()); }
    public LootPoolEntry(Collection<? extends LootCondition> conditions) {
        this.conditions = List.copyOf(conditions == null ? List.of() : conditions);
    }

    public List<LootCondition> getConditions() { return conditions; }
    public LootPoolEntryType getType() { return null; }
    public boolean test(LootContext context) {
        for (LootCondition condition : conditions) if (condition != null && !condition.test(context)) return false;
        return true;
    }

    public static class Builder {
        private final List<LootCondition> conditions = new ArrayList<>();

        public Builder() { }
        protected Builder(Collection<? extends LootCondition> conditions) {
            if (conditions != null) this.conditions.addAll(conditions);
        }
        public Builder conditionally(LootCondition.Builder conditionBuilder) {
            if (conditionBuilder != null) conditionally(conditionBuilder.build());
            return this;
        }
        public Builder conditionally(LootCondition condition) {
            if (condition != null) conditions.add(condition);
            return this;
        }
        public List<LootCondition> getConditions() { return List.copyOf(conditions); }
        public Builder getThisBuilder() { return this; }
        public Builder getThisConditionConsumingBuilder() { return this; }
        public LootPoolEntry build() { return new LootPoolEntry(conditions); }
    }
}
