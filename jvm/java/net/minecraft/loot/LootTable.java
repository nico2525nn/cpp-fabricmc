package net.minecraft.loot;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Optional;
import net.minecraft.loot.function.LootFunction;
import net.minecraft.util.Identifier;
import net.minecraft.util.context.ContextType;

/**
 * Server-side loot-table anchor for the 1.21.4 shadow ABI.
 *
 * <p>Fabric's loot events expose this type in their erased callback
 * descriptors, so it must exist even when the native server is not asked to
 * generate loot.  The actual vanilla loot-table data remains owned by the
 * native world implementation; this class is deliberately kept free of a
 * second data-pack parser.</p>
 */
public class LootTable {
    // Fabric's LootTableAccessor mixin reads these fields by their named
    // selectors.  Keep the storage shape compatible even though data-pack
    // loading itself is owned by the native server.
    private final List<LootPool> pools;
    private final List<LootFunction> functions;
    private final Optional<Identifier> randomSequenceId;
    private final ContextType type;

    public LootTable() {
        this(ContextType.builder().build(), Optional.empty(), List.of(), List.of());
    }

    public LootTable(ContextType type, Optional<Identifier> randomSequenceId,
                     List<? extends LootFunction> functions,
                     List<? extends LootPool> pools) {
        this.type = type == null ? ContextType.builder().build() : type;
        this.randomSequenceId = randomSequenceId == null ? Optional.empty() : randomSequenceId;
        this.functions = List.copyOf(functions == null ? List.of() : functions);
        this.pools = List.copyOf(pools == null ? List.of() : pools);
    }

    public ContextType getType() { return type; }
    public List<LootPool> getPools() { return pools; }
    public List<LootFunction> getFunctions() { return functions; }
    public Optional<Identifier> getRandomSequenceId() { return randomSequenceId; }

    public static Builder builder() { return new Builder(); }

    /** Builder anchor used by Fabric's loot event callback descriptors. */
    public static class Builder implements
            net.fabricmc.fabric.api.loot.v2.FabricLootTableBuilder,
            net.fabricmc.fabric.api.loot.v3.FabricLootTableBuilder {
        private ContextType type = ContextType.builder().build();
        private final List<LootPool> pools = new ArrayList<>();
        private final List<LootFunction> functions = new ArrayList<>();
        private Optional<Identifier> randomSequenceId = Optional.empty();

        public Builder() { }

        public Builder type(ContextType type) {
            this.type = type == null ? ContextType.builder().build() : type;
            return this;
        }

        public Builder pool(LootPool.Builder poolBuilder) {
            if (poolBuilder != null) pools.add(poolBuilder.build());
            return this;
        }

        @Override
        public Builder pool(LootPool pool) {
            if (pool != null) pools.add(pool);
            return this;
        }

        @Override
        public Builder pools(Collection<? extends LootPool> values) {
            if (values != null) for (LootPool pool : values) pool(pool);
            return this;
        }

        public Builder apply(LootFunction.Builder functionBuilder) {
            if (functionBuilder != null) {
                LootFunction function = functionBuilder.build();
                if (function != null) functions.add(function);
            }
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

        @Override
        public Builder modifyPools(java.util.function.Consumer<? super LootPool.Builder> modifier) {
            if (modifier == null) return this;
            List<LootPool> modified = new ArrayList<>(pools.size());
            for (LootPool pool : pools) {
                LootPool.Builder poolBuilder = LootPool.Builder.copyOf(pool);
                modifier.accept(poolBuilder);
                modified.add(poolBuilder.build());
            }
            pools.clear();
            pools.addAll(modified);
            return this;
        }

        public Builder randomSequenceId(Identifier id) {
            randomSequenceId = Optional.ofNullable(id);
            return this;
        }

        public LootTable build() {
            return new LootTable(type, randomSequenceId, functions, pools);
        }
    }
}
