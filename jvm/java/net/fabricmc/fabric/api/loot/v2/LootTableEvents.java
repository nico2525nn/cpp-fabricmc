package net.fabricmc.fabric.api.loot.v2;

import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.loot.LootTable;
import net.minecraft.registry.Registry;
import net.minecraft.registry.RegistryKey;
import net.minecraft.resource.ResourceManager;

/** Deprecated v2 loot events retained for 1.21.4 mod ABI compatibility. */
@Deprecated
public final class LootTableEvents {
    private LootTableEvents() { }

    public static final Event<Replace> REPLACE = EventFactory.createArrayBacked(Replace.class,
        listeners -> (key, original, source) -> {
            for (Replace listener : listeners) {
                LootTable replacement = listener.replaceLootTable(key, original, source);
                if (replacement != null) return replacement;
            }
            return null;
        });
    public static final Event<Modify> MODIFY = EventFactory.createArrayBacked(Modify.class,
        listeners -> (key, builder, source) -> {
            for (Modify listener : listeners) listener.modifyLootTable(key, builder, source);
        });
    public static final Event<Loaded> ALL_LOADED = EventFactory.createArrayBacked(Loaded.class,
        listeners -> (resourceManager, lootRegistry) -> {
            for (Loaded listener : listeners) listener.onLootTablesLoaded(resourceManager, lootRegistry);
        });

    @FunctionalInterface
    public interface Replace {
        LootTable replaceLootTable(RegistryKey<LootTable> key, LootTable original, LootTableSource source);
    }
    @FunctionalInterface
    public interface Modify {
        void modifyLootTable(RegistryKey<LootTable> key, LootTable.Builder tableBuilder, LootTableSource source);
    }
    @FunctionalInterface
    public interface Loaded {
        void onLootTablesLoaded(ResourceManager resourceManager, Registry<LootTable> lootRegistry);
    }
}
