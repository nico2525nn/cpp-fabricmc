package net.minecraft.registry;

import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Stream;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.registry.entry.RegistryEntryOwner;
import net.minecraft.util.Identifier;

/**
 * The 1.21.4 registry contract. Vanilla exposes this type as an interface;
 * mutable storage is supplied by {@link SimpleRegistry}. Keeping the
 * contract separate is important because Fabric's registry mixins use
 * {@code invokeinterface} and add listener interfaces to the concrete type.
 */
public interface Registry<T> extends Iterable<T>, RegistryEntryOwner<T> {
    @FunctionalInterface
    interface EntryAddedListener<T> {
        void onEntryAdded(int rawId, Identifier id, T entry);
    }

    RegistryKey<?> getKey();
    T get(Identifier id);

    default T get(RegistryKey<T> key) { return key == null ? null : get(key.getValue()); }
    default T getValueOrThrow(RegistryKey<T> key) {
        T value = get(key);
        if (value == null) throw new IllegalStateException("unknown registry key: " + key);
        return value;
    }
    default Optional<T> getOptionalValue(Identifier id) { return Optional.ofNullable(get(id)); }

    Optional<RegistryEntry<T>> getEntry(Identifier id);
    Optional<RegistryEntry<T>> getEntry(T value);
    default RegistryEntry<T> getEntryOrThrow(Identifier id) {
        return getEntry(id).orElseThrow(() -> new IllegalArgumentException("unknown registry id: " + id));
    }
    default RegistryEntry<T> getEntryOrThrow(RegistryKey<T> key) {
        return getEntryOrThrow(key == null ? null : key.getValue());
    }
    default Optional<RegistryEntry<T>> getEntry(int rawId) { return Optional.empty(); }

    default int getRawId(T value) {
        Identifier id = getId(value);
        if (id == null) return -1;
        int index = 0;
        for (Identifier candidate : getIds()) {
            if (candidate.equals(id)) return index;
            index++;
        }
        return -1;
    }
    default Identifier getId(T value) { return null; }
    default int size() { return getIds().size(); }
    default boolean containsId(Identifier id) { return get(id) != null; }
    default boolean contains(RegistryKey<T> key) { return get(key) != null; }
    default Set<Identifier> getIds() { return Set.of(); }
    default Set<Map.Entry<Identifier, T>> entrySet() { return Set.of(); }
    default Stream<T> stream() { return java.util.stream.StreamSupport.stream(spliterator(), false); }
    default Stream<net.minecraft.registry.tag.TagKey<T>> streamTags() { return Stream.empty(); }
    default Registry<T> freeze() { return this; }
    default void registerValue(Identifier id, T value) {
        throw new UnsupportedOperationException("registry is read-only: " + getKey());
    }
    default void addTag(net.minecraft.registry.tag.TagKey<T> tag, T value) { }
    /** Returns whether this registry has associated membership for the tag. */
    default boolean hasTag(net.minecraft.registry.tag.TagKey<T> tag, T value) { return false; }
    default void addEntryListener(EntryAddedListener<T> listener) { }
    default boolean removeEntryListener(EntryAddedListener<T> listener) { return false; }

    /** Vanilla's static convenience registration method. */
    static <T> T register(Registry<? super T> registry, Identifier id, T value) {
        if (registry == null || id == null || value == null)
            throw new NullPointerException("registry/id/value");
        @SuppressWarnings("unchecked") Registry<T> typed = (Registry<T>) registry;
        typed.registerValue(id, value);
        return value;
    }

    static <T> RegistryEntry.Reference<T> registerReference(Registry<T> registry,
                                                              Identifier id, T value) {
        register(registry, id, value);
        return new RegistryEntry.Reference<>(value, RegistryKey.of(registry.getKey(), id));
    }

    static <T> RegistryEntry.Reference<T> registerReference(Registry<T> registry,
                                                              RegistryKey<T> key, T value) {
        return registerReference(registry, key == null ? null : key.getValue(), value);
    }

    static <T> boolean containsTag(net.minecraft.registry.tag.TagKey<T> tag, T value) {
        if (tag == null || value == null) return false;
        Registry<?> registry = Registries.byKey(tag.registry());
        if (registry == null) return false;
        @SuppressWarnings("unchecked") Registry<T> typed = (Registry<T>) registry;
        return typed.hasTag(tag, value);
    }
    static <T> boolean containsTag(net.minecraft.registry.TagKey<T> tag, T value) {
        return false;
    }
}
