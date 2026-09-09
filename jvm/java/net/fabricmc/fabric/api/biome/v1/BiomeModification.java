package net.fabricmc.fabric.api.biome.v1;

import java.util.Objects;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import java.util.function.Predicate;
import net.fabricmc.fabric.impl.biome.modification.BiomeModificationImpl;
import net.minecraft.util.Identifier;

/** A named, ordered set of callbacks applied when biome data is finalized. */
public class BiomeModification {
    private final Identifier id;

    BiomeModification(Identifier id) { this.id = Objects.requireNonNull(id, "id"); }

    public BiomeModification add(ModificationPhase phase, Predicate<BiomeSelectionContext> selector,
            Consumer<BiomeModificationContext> modifier) {
        BiomeModificationImpl.INSTANCE.addModifier(id, phase, selector, modifier);
        return this;
    }

    public BiomeModification add(ModificationPhase phase, Predicate<BiomeSelectionContext> selector,
            BiConsumer<BiomeSelectionContext, BiomeModificationContext> modifier) {
        BiomeModificationImpl.INSTANCE.addModifier(id, phase, selector, modifier);
        return this;
    }
}
