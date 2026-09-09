package net.minecraft.loot.condition;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.loot.context.LootContext;

public final class AllOfLootCondition implements LootCondition {
    private final List<LootCondition> terms;
    public AllOfLootCondition(List<? extends LootCondition> terms) { this.terms = List.copyOf(terms == null ? List.of() : terms); }
    @Override public boolean test(LootContext context) {
        for (LootCondition term : terms) if (term != null && !term.test(context)) return false;
        return true;
    }
    public static Builder builder(LootCondition.Builder... terms) { return new Builder(terms); }
    public static final class Builder implements LootCondition.Builder {
        private final List<LootCondition.Builder> terms = new ArrayList<>();
        private Builder(LootCondition.Builder... values) { if (values != null) for (LootCondition.Builder value : values) if (value != null) terms.add(value); }
        @Override public Builder and(LootCondition.Builder condition) { if (condition != null) terms.add(condition); return this; }
        @Override public AllOfLootCondition build() {
            List<LootCondition> built = new ArrayList<>();
            for (LootCondition.Builder term : terms) built.add(term.build());
            return new AllOfLootCondition(built);
        }
    }
}
