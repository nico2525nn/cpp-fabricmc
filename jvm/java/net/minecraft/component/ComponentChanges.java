package net.minecraft.component;

import com.mojang.serialization.Codec;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.function.Predicate;
import it.unimi.dsi.fastutil.objects.Reference2ObjectMap;
import it.unimi.dsi.fastutil.objects.Reference2ObjectOpenHashMap;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;

/**
 * Immutable set of component overrides.
 *
 * <p>An {@link Optional#empty()} value means that a component is explicitly
 * removed from the holder; a present value adds or replaces it. This is the
 * representation used by the 1.21.4 recipe component ingredient API.</p>
 */
public final class ComponentChanges {
    public static final ComponentChanges EMPTY =
        new ComponentChanges(new Reference2ObjectOpenHashMap<>());
    public static final Codec<ComponentChanges> CODEC = new Codec<>() { };
    public static final PacketCodec<RegistryByteBuf, ComponentChanges> PACKET_CODEC =
        PacketCodec.ofLegacy((buffer, value) -> { }, buffer -> EMPTY);

    private static final String REMOVE_PREFIX = "!";
    final Reference2ObjectMap<ComponentType<?>, Optional<?>> changedComponents;

    public ComponentChanges(Reference2ObjectMap<ComponentType<?>, Optional<?>> changedComponents) {
        Reference2ObjectMap<ComponentType<?>, Optional<?>> copy = new Reference2ObjectOpenHashMap<>();
        if (changedComponents != null) copy.putAll(changedComponents);
        this.changedComponents = copy;
    }

    public static Builder builder() { return new Builder(); }

    @SuppressWarnings("unchecked")
    public <T> Optional<? extends T> get(ComponentType<? extends T> type) {
        return (Optional<? extends T>) changedComponents.get(type);
    }

    public Set<Map.Entry<ComponentType<?>, Optional<?>>> entrySet() {
        return Collections.unmodifiableSet(changedComponents.entrySet());
    }

    public int size() { return changedComponents.size(); }
    public boolean isEmpty() { return changedComponents.isEmpty(); }

    public ComponentChanges withRemovedIf(Predicate<ComponentType<?>> predicate) {
        if (predicate == null) return this;
        Reference2ObjectMap<ComponentType<?>, Optional<?>> copy = new Reference2ObjectOpenHashMap<>();
        changedComponents.forEach((type, value) -> {
            if (value.isPresent() || !predicate.test(type)) copy.put(type, value);
        });
        return new ComponentChanges(copy);
    }

    public AddedRemovedPair toAddedRemovedPair() {
        Map<ComponentType<?>, Object> added = new LinkedHashMap<>();
        Set<ComponentType<?>> removed = new LinkedHashSet<>();
        changedComponents.forEach((type, value) -> {
            if (value.isPresent()) added.put(type, value.get());
            else removed.add(type);
        });
        return new AddedRemovedPair(ComponentMap.of(added), Set.copyOf(removed));
    }

    @Override public boolean equals(Object other) {
        return other instanceof ComponentChanges changes
            && changedComponents.equals(changes.changedComponents);
    }

    @Override public int hashCode() { return changedComponents.hashCode(); }

    @Override public String toString() { return toString(changedComponents); }

    static String toString(Reference2ObjectMap<ComponentType<?>, Optional<?>> changes) {
        Map<String, Object> printable = new LinkedHashMap<>();
        changes.forEach((type, value) -> printable.put(
            value.isPresent() ? String.valueOf(type) : REMOVE_PREFIX + type,
            value.orElse(null)));
        return printable.toString();
    }

    /** Pair used when a change set is split into additions and removals. */
    public record AddedRemovedPair(ComponentMap added, Set<ComponentType<?>> removed) {
        public static final AddedRemovedPair EMPTY = new AddedRemovedPair(ComponentMap.EMPTY, Set.of());
    }

    /** Mutable construction API matching ComponentChanges.Builder. */
    public static class Builder {
        private final Reference2ObjectMap<ComponentType<?>, Optional<?>> changes =
            new Reference2ObjectOpenHashMap<>();

        public Builder() { }

        public <T> Builder add(ComponentType<T> type, T value) {
            if (type == null) throw new NullPointerException("type");
            if (value == null) throw new NullPointerException("value");
            changes.put(type, Optional.of(value));
            return this;
        }

        public <T> Builder remove(ComponentType<T> type) {
            if (type == null) throw new NullPointerException("type");
            changes.put(type, Optional.empty());
            return this;
        }

        public <T> Builder add(Component<T> component) {
            if (component == null) throw new NullPointerException("component");
            return add(component.type(), component.value());
        }

        public ComponentChanges build() { return new ComponentChanges(changes); }
    }
}
