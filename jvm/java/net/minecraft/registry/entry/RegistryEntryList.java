package net.minecraft.registry.entry;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import java.util.Optional;
import java.util.function.Function;
import java.util.stream.Stream;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.util.math.random.Random;

/** Immutable registry-entry list contract, including the named tag view. */
public interface RegistryEntryList<T> extends Iterable<RegistryEntry<T>> {
    default boolean contains(RegistryEntry<T> entry) { return stream().anyMatch(value -> value == entry || value.equals(entry)); }
    default Optional<RegistryEntry<T>> getRandom(Random random) {
        if (size() == 0) return Optional.empty();
        return Optional.of(get(random == null ? 0 : random.nextInt(size())));
    }
    default RegistryEntry<T> get(int index) { return stream().toList().get(index); }
    default int size() { return (int) stream().count(); }
    default boolean isBound() { return true; }
    default Stream<RegistryEntry<T>> stream() {
        List<RegistryEntry<T>> values = new ArrayList<>();
        for (RegistryEntry<T> entry : this) values.add(entry);
        return values.stream();
    }
    default Optional<TagKey<T>> getTagKey() { return Optional.empty(); }
    default boolean ownerEquals(RegistryEntryOwner<T> owner) { return false; }
    default Object getStorage() { return List.of(); }

    static <T> Direct<T> of(List<RegistryEntry<T>> entries) { return new Direct<>(entries); }
    @SafeVarargs static <T> Direct<T> of(RegistryEntry<T>... entries) { return new Direct<>(List.of(entries)); }
    static <T, V> Direct<T> of(Function<V, RegistryEntry<T>> mapper, Collection<V> values) {
        List<RegistryEntry<T>> entries = new ArrayList<>();
        if (values != null) for (V value : values) entries.add(mapper.apply(value));
        return new Direct<>(entries);
    }
    @SafeVarargs static <T, V> Direct<T> of(Function<V, RegistryEntry<T>> mapper, V... values) {
        List<RegistryEntry<T>> entries = new ArrayList<>();
        if (values != null) for (V value : values) entries.add(mapper.apply(value));
        return new Direct<>(entries);
    }
    static <T> Direct<T> empty() { return new Direct<>(List.of()); }

    final class Direct<T> implements RegistryEntryList<T> {
        public static final Direct<?> EMPTY = new Direct<>(List.of());
        private final List<RegistryEntry<T>> entries;
        public Direct(List<RegistryEntry<T>> entries) {
            this.entries = List.copyOf(entries == null ? List.of() : entries);
        }
        @Override public Iterator<RegistryEntry<T>> iterator() { return entries.iterator(); }
        @Override public Stream<RegistryEntry<T>> stream() { return entries.stream(); }
        @Override public int size() { return entries.size(); }
        @Override public RegistryEntry<T> get(int index) { return entries.get(index); }
        @Override public boolean equals(Object other) { return other instanceof Direct<?> d && entries.equals(d.entries); }
        @Override public int hashCode() { return entries.hashCode(); }
    }

    class ListBacked<T> implements RegistryEntryList<T> {
        protected final List<RegistryEntry<T>> entries = new ArrayList<>();
        @Override public Iterator<RegistryEntry<T>> iterator() { return entries.iterator(); }
        public List<RegistryEntry<T>> getEntries() { return Collections.unmodifiableList(entries); }
    }

    class Named<T> extends ListBacked<T> {
        private final RegistryEntryOwner<T> owner;
        private final TagKey<T> tag;
        public Named(RegistryEntryOwner<T> owner, TagKey<T> tag) {
            this.owner = owner;
            this.tag = tag;
        }
        public void setEntries(List<RegistryEntry<T>> entries) {
            this.entries.clear();
            if (entries != null) this.entries.addAll(entries);
        }
        public TagKey<T> getTag() { return tag; }
        @Override public Optional<TagKey<T>> getTagKey() { return Optional.ofNullable(tag); }
        @Override public boolean ownerEquals(RegistryEntryOwner<T> value) { return owner == value || (owner != null && owner.ownerEquals(value)); }
        @Override public boolean isBound() { return !entries.isEmpty(); }
    }
}
