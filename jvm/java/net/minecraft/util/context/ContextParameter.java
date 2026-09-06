package net.minecraft.util.context;

import java.util.Objects;
import net.minecraft.util.Identifier;

/** A typed key used by Minecraft's loot and command context APIs. */
public final class ContextParameter<T> {
    private final Identifier id;

    public ContextParameter(Identifier id) {
        this.id = Objects.requireNonNull(id, "id");
    }

    public Identifier getId() { return id; }

    public static <T> ContextParameter<T> of(String id) {
        return new ContextParameter<>(Identifier.of(id));
    }

    @Override public boolean equals(Object other) {
        return other instanceof ContextParameter<?> parameter && id.equals(parameter.id);
    }

    @Override public int hashCode() { return id.hashCode(); }
    @Override public String toString() { return id.toString(); }
}
