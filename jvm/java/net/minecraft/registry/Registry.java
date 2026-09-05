package net.minecraft.registry;

import net.minecraft.util.Identifier;
import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Stream;

public class Registry<T> implements Iterable<T> {
    private final RegistryKey<?> registryKey;
    private final Map<Identifier, T> values = new ConcurrentHashMap<>();
    private final Map<T, Identifier> reverse = new ConcurrentHashMap<>();
    private final Map<TagKey<T>, Set<T>> tags = new ConcurrentHashMap<>();
    public Registry() { this(null); }
    public Registry(RegistryKey<?> registryKey) { this.registryKey = registryKey; }
    public T get(Identifier id) { return values.get(id); }
    public T get(RegistryKey<T> key) { return key == null ? null : get(key.getValue()); }
    public T getOrThrow(Identifier id) {
        T value = get(id);
        if (value == null) throw new IllegalArgumentException("unknown registry id: " + id);
        return value;
    }
    public T getOrThrow(RegistryKey<T> key) { return getOrThrow(key == null ? null : key.getValue()); }
    public boolean containsId(Identifier id) { return values.containsKey(id); }
    public int size() { return values.size(); }
    public Identifier getId(T value) {
        return value == null ? null : reverse.get(value);
    }
    public int getRawId(T value) {
        Identifier id = getId(value);
        if (id == null) return -1;
        int raw = 0;
        for (Identifier candidate : values.keySet()) {
            if (candidate.equals(id)) return raw;
            raw++;
        }
        return -1;
    }
    public T get(int rawId) {
        if (rawId < 0) return null;
        int index = 0;
        for (T value : values.values()) if (index++ == rawId) return value;
        return null;
    }
    public Optional<RegistryEntry<T>> getEntry(Identifier id) {
        T value = get(id);
        return value == null ? Optional.empty() : Optional.of(entry(id, value));
    }
    public Optional<RegistryEntry<T>> getEntry(T value) {
        Identifier id = getId(value);
        return id == null ? Optional.empty() : Optional.of(entry(id, value));
    }
    public RegistryEntry<T> getEntryOrThrow(Identifier id) { return getEntry(id).orElseThrow(() -> new IllegalArgumentException("unknown registry id: " + id)); }
    public RegistryEntry<T> getEntryOrThrow(RegistryKey<T> key) { return getEntryOrThrow(key == null ? null : key.getValue()); }
    public RegistryKey<?> getKey() { return registryKey; }
    public Set<Identifier> getIds() { return Collections.unmodifiableSet(new LinkedHashSet<>(values.keySet())); }
    public Set<Map.Entry<Identifier, T>> entrySet() { return Collections.unmodifiableSet(new LinkedHashSet<>(values.entrySet())); }
    public Stream<T> stream() { return values.values().stream(); }
    public Registry<T> freeze() { return this; }
    public void addTag(TagKey<T> tag, T value) { if (tag != null && value != null) tags.computeIfAbsent(tag, ignored -> ConcurrentHashMap.newKeySet()).add(value); }
    boolean isInTag(TagKey<T> tag, T value) { return tag != null && value != null && tags.getOrDefault(tag, Set.of()).contains(value); }
    private RegistryEntry<T> entry(Identifier id, T value) {
        RegistryKey<T> key = registryKey == null ? new RegistryKey<>(id) : RegistryKey.of(registryKey, id);
        return RegistryEntry.of(key, value);
    }
    static <T> boolean containsTag(TagKey<T> tag, T value) {
        Registry<?> registry = Registries.byKey(tag.registry());
        if (registry == null) return false;
        @SuppressWarnings("unchecked") Registry<T> typed = (Registry<T>) registry;
        return typed.isInTag(tag, value);
    }
    @Override public Iterator<T> iterator() { return new ArrayList<>(values.values()).iterator(); }

    @SuppressWarnings("unchecked")
    public static <T> T register(Registry<? super T> registry, Identifier id, T value) {
        if (registry == null || id == null || value == null) throw new NullPointerException("registry/id/value");
        Registry<T> typed = (Registry<T>) registry;
        T previous = typed.values.putIfAbsent(id, value);
        if (previous != null) throw new IllegalArgumentException("duplicate registry id: " + id);
        typed.reverse.put(value, id);
        return value;
    }
    public static <T> RegistryEntry.Reference<T> registerReference(Registry<T> registry, Identifier id, T value) {
        register(registry, id, value);
        RegistryKey<T> key = registry.getKey() == null ? new RegistryKey<>(id) : RegistryKey.of(registry.getKey(), id);
        return RegistryEntry.Reference.of(key, value);
    }
}
