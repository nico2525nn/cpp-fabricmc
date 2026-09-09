package net.minecraft.loot.condition;

import net.minecraft.loot.context.LootContext;

/** Server loot predicate contract. */
public interface LootCondition {
    default LootConditionType getType() { return null; }
    default boolean test(LootContext context) { return true; }

    interface Builder {
        LootCondition build();
        default AnyOfLootCondition.Builder or(Builder condition) {
            return AnyOfLootCondition.builder(this, condition);
        }
        default AllOfLootCondition.Builder and(Builder condition) {
            return AllOfLootCondition.builder(this, condition);
        }
        default InvertedLootCondition.Builder invert() {
            return InvertedLootCondition.builder(this);
        }
    }
}
