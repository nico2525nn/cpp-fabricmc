package net.minecraft.registry;

import net.minecraft.util.Identifier;
import net.minecraft.block.Block;
import net.minecraft.entity.EntityType;
import net.minecraft.fluid.Fluid;
import net.minecraft.item.Item;
import net.minecraft.util.NativeAccess;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Stream;

public class Registry<T> implements Iterable<T>, net.minecraft.registry.entry.RegistryEntryOwner<T> {
    private final RegistryKey<?> registryKey;
    private final Map<Identifier, T> values = new java.util.LinkedHashMap<>();
    private final Map<T, Identifier> reverse = new HashMap<>();
    private final Map<TagKey<T>, Set<T>> tags = new HashMap<>();
    private final List<EntryAddedListener<T>> entryListeners = new ArrayList<>();
    @FunctionalInterface public interface EntryAddedListener<T> {
        void onEntryAdded(int rawId, Identifier id, T entry);
    }
    public Registry() { this(null); }
    public Registry(RegistryKey<?> registryKey) { this.registryKey = registryKey; }
    public synchronized T get(Identifier id) {
        if (id == null) return null;
        T value = values.get(id);
        if (value != null) return value;
        value = resolveNative(id);
        if (value != null) {
            values.put(id, value);
            reverse.put(value, id);
        }
        return value;
    }
    public synchronized T get(RegistryKey<T> key) { return key == null ? null : get(key.getValue()); }
    public synchronized T getOrThrow(Identifier id) {
        T value = get(id);
        if (value == null) throw new IllegalArgumentException("unknown registry id: " + id);
        return value;
    }
    public synchronized T getOrThrow(RegistryKey<T> key) { return getOrThrow(key == null ? null : key.getValue()); }
    public synchronized boolean containsId(Identifier id) { return get(id) != null; }
    public synchronized int size() { return Math.max(values.size(), nativeSize()); }
    public synchronized Identifier getId(T value) {
        return value == null ? null : reverse.get(value);
    }
    public synchronized int getRawId(T value) {
        Identifier id = getId(value);
        if (id == null) return -1;
        int raw = 0;
        for (Identifier candidate : values.keySet()) {
            if (candidate.equals(id)) return raw;
            raw++;
        }
        return -1;
    }
    public synchronized T get(int rawId) {
        if (rawId < 0) return null;
        int index = 0;
        for (T value : values.values()) if (index++ == rawId) return value;
        String name = NativeAccess.registryEntryName(nativeRegistryName(), rawId);
        Identifier id = Identifier.tryParse(name);
        if (id != null) return get(id);
        return null;
    }
    public synchronized Optional<RegistryEntry<T>> getEntry(Identifier id) {
        T value = get(id);
        return value == null ? Optional.empty() : Optional.of(entry(id, value));
    }
    public synchronized Optional<RegistryEntry<T>> getEntry(T value) {
        Identifier id = getId(value);
        return id == null ? Optional.empty() : Optional.of(entry(id, value));
    }
    public synchronized RegistryEntry<T> getEntryOrThrow(Identifier id) { return getEntry(id).orElseThrow(() -> new IllegalArgumentException("unknown registry id: " + id)); }
    public synchronized RegistryEntry<T> getEntryOrThrow(RegistryKey<T> key) { return getEntryOrThrow(key == null ? null : key.getValue()); }
    public RegistryKey<?> getKey() { return registryKey; }
    @Override public boolean ownerEquals(net.minecraft.registry.entry.RegistryEntryOwner<T> other) { return this == other; }
    public synchronized Set<Identifier> getIds() {
        LinkedHashSet<Identifier> result = new LinkedHashSet<>(values.keySet());
        int count = nativeSize();
        for (int rawId = 0; rawId < count; rawId++) {
            Identifier id = Identifier.tryParse(NativeAccess.registryEntryName(nativeRegistryName(), rawId));
            if (id != null) result.add(id);
        }
        return Collections.unmodifiableSet(result);
    }
    public synchronized Set<Map.Entry<Identifier, T>> entrySet() { return Collections.unmodifiableSet(new LinkedHashSet<>(values.entrySet())); }
    public synchronized Stream<T> stream() { return new ArrayList<>(values.values()).stream(); }
    public Registry<T> freeze() { return this; }
    public synchronized void addTag(TagKey<T> tag, T value) { if (tag != null && value != null) tags.computeIfAbsent(tag, ignored -> new LinkedHashSet<>()).add(value); }
    synchronized boolean isInTag(TagKey<T> tag, T value) { return tag != null && value != null && tags.getOrDefault(tag, Set.of()).contains(value); }
    public synchronized void addEntryListener(EntryAddedListener<T> listener) {
        if (listener != null) entryListeners.add(listener);
    }
    public synchronized boolean removeEntryListener(EntryAddedListener<T> listener) { return entryListeners.remove(listener); }
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
    @Override public synchronized Iterator<T> iterator() { return new ArrayList<>(values.values()).iterator(); }

    @SuppressWarnings("unchecked")
    public static <T> T register(Registry<? super T> registry, Identifier id, T value) {
        if (registry == null || id == null || value == null) throw new NullPointerException("registry/id/value");
        Registry<T> typed = (Registry<T>) registry;
        List<EntryAddedListener<T>> listeners;
        int rawId;
        synchronized (typed) {
            if (typed.values.containsKey(id)) throw new IllegalArgumentException("duplicate registry id: " + id);
            rawId = Math.max(typed.values.size(), typed.nativeSize());
            typed.values.put(id, value);
            typed.reverse.put(value, id);
            listeners = List.copyOf(typed.entryListeners);
        }
        for (EntryAddedListener<T> listener : listeners) listener.onEntryAdded(rawId, id, value);
        return value;
    }
    public static <T> RegistryEntry.Reference<T> registerReference(Registry<T> registry, Identifier id, T value) {
        register(registry, id, value);
        RegistryKey<T> key = registry.getKey() == null ? new RegistryKey<>(id) : RegistryKey.of(registry.getKey(), id);
        return RegistryEntry.Reference.of(key, value);
    }

    private String nativeRegistryName() {
        if (registryKey == null || registryKey.getValue() == null) return "";
        Identifier value = registryKey.getValue();
        return "minecraft".equals(value.getNamespace()) ? value.getPath() : value.toString();
    }

    private int nativeSize() {
        String name = nativeRegistryName();
        return switch (name) {
            case "item", "block", "entity_type", "fluid" -> NativeAccess.registryEntryCount(name);
            default -> 0;
        };
    }

    @SuppressWarnings("unchecked")
    private T resolveNative(Identifier id) {
        String name = nativeRegistryName();
        return switch (name) {
            case "item" -> {
                int rawId = NativeAccess.registryItemId(id.toString());
                yield rawId < 0 ? null : (T) new Item(id, rawId);
            }
            case "block" -> {
                int rawState = NativeAccess.registryBlockState(id.toString());
                yield rawState < 0 ? null : (T) new Block(rawState, id);
            }
            case "entity_type" -> hasNativeEntry(name, id)
                ? (T) new EntityType<>(id, () -> null, 0.6f, 1.8f) : null;
            case "fluid" -> hasNativeEntry(name, id) ? (T) new Fluid(id) : null;
            default -> null;
        };
    }

    private boolean hasNativeEntry(String registry, Identifier id) {
        int count = Math.min(nativeSize(), 100_000);
        for (int rawId = 0; rawId < count; rawId++) {
            if (id.equals(Identifier.tryParse(NativeAccess.registryEntryName(registry, rawId)))) return true;
        }
        return false;
    }
}
