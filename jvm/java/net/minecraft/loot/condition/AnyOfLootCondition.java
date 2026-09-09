package net.minecraft.loot.condition;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.loot.context.LootContext;

public final class AnyOfLootCondition implements LootCondition {
    private final List<LootCondition> terms;
    public AnyOfLootCondition(List<? extends LootCondition> terms) { this.terms = List.copyOf(terms == null ? List.of() : terms); }
    @Override public boolean test(LootContext context) {
        if (terms.isEmpty()) return false;
        for (LootCondition term : terms) if (term != null && term.test(context)) return true;
        return false;
    }
    public static Builder builder(LootCondition.Builder... terms) { return new Builder(terms); }
    public static final class Builder implements LootCondition.Builder {
        private final List<LootCondition.Builder> terms = new ArrayList<>();
        private Builder(LootCondition.Builder... values) { if (values != null) for (LootCondition.Builder value : values) if (value != null) terms.add(value); }
        @Override public Builder or(LootCondition.Builder condition) { if (condition != null) terms.add(condition); return this; }
        @Override public AnyOfLootCondition build() {
            List<LootCondition> built = new ArrayList<>();
            for (LootCondition.Builder term : terms) built.add(term.build());
            return new AnyOfLootCondition(built);
        }
    }
}
