package net.fabricmc.fabric.impl.biome.modification;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Objects;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import java.util.function.Predicate;
import net.fabricmc.fabric.api.biome.v1.BiomeModificationContext;
import net.fabricmc.fabric.api.biome.v1.BiomeSelectionContext;
import net.fabricmc.fabric.api.biome.v1.ModificationPhase;
import net.minecraft.registry.RegistryKey;
import net.minecraft.util.Identifier;
import net.minecraft.world.biome.Biome;

/** Process-local registry of ordered biome modifiers. */
public final class BiomeModificationImpl {
    public static final BiomeModificationImpl INSTANCE = new BiomeModificationImpl();
    private final List<ModifierRecord> modifiers = new ArrayList<>();
    private BiomeModificationImpl() { }

    public synchronized void addModifier(Identifier id, ModificationPhase phase,
            Predicate<BiomeSelectionContext> selector, Consumer<BiomeModificationContext> modifier) {
        modifiers.add(new ModifierRecord(id, phase, selector, modifier, null));
    }
    public synchronized void addModifier(Identifier id, ModificationPhase phase,
            Predicate<BiomeSelectionContext> selector,
            BiConsumer<BiomeSelectionContext, BiomeModificationContext> modifier) {
        modifiers.add(new ModifierRecord(id, phase, selector, null, modifier));
    }

    /** Apply all registered modifiers to one biome in Fabric's phase/id order. */
    public void apply(RegistryKey<Biome> key, Biome biome) {
        if (key == null || biome == null) return;
        BiomeSelectionContext selection = new BiomeSelectionContextImpl(key, biome);
        List<ModifierRecord> snapshot;
        synchronized (this) { snapshot = modifiers.stream().sorted(Comparator
            .comparing((ModifierRecord record) -> record.phase.ordinal())
            .thenComparing(record -> record.id.toString())).toList(); }
        BiomeModificationContext context = null;
        for (ModifierRecord record : snapshot) {
            if (record.selector != null && record.selector.test(selection)) {
                if (context == null) context = new BiomeModificationContextImpl(biome);
                record.apply(selection, context);
            }
        }
    }

    public synchronized void clear() { modifiers.clear(); }

    private static final class ModifierRecord {
        private final Identifier id;
        private final ModificationPhase phase;
        private final Predicate<BiomeSelectionContext> selector;
        private final Consumer<BiomeModificationContext> modifier;
        private final BiConsumer<BiomeSelectionContext, BiomeModificationContext> sensitive;
        private ModifierRecord(Identifier id, ModificationPhase phase, Predicate<BiomeSelectionContext> selector,
                Consumer<BiomeModificationContext> modifier,
                BiConsumer<BiomeSelectionContext, BiomeModificationContext> sensitive) {
            this.id = Objects.requireNonNull(id, "id");
            this.phase = phase == null ? ModificationPhase.ADDITIONS : phase;
            this.selector = Objects.requireNonNull(selector, "selector");
            this.modifier = modifier;
            this.sensitive = sensitive;
        }
        private void apply(BiomeSelectionContext selection, BiomeModificationContext context) {
            if (sensitive != null) sensitive.accept(selection, context);
            else modifier.accept(context);
        }
    }
}
