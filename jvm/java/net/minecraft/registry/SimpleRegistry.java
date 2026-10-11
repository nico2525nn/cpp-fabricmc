package net.minecraft.registry;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Stream;
import com.mojang.serialization.Lifecycle;
import it.unimi.dsi.fastutil.objects.ObjectArrayList;
import it.unimi.dsi.fastutil.objects.ObjectList;
import it.unimi.dsi.fastutil.objects.Reference2IntMap;
import it.unimi.dsi.fastutil.objects.Reference2IntOpenHashMap;
import net.minecraft.block.Block;
import net.minecraft.entity.EntityType;
import net.minecraft.fluid.Fluid;
import net.minecraft.item.Item;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.registry.entry.RegistryEntryInfo;
import net.minecraft.util.NativeAccess;

/** Native-backed concrete registry used by the Java shadow runtime. */
public class SimpleRegistry<T> implements MutableRegistry<T> {
    // Names and descriptors mirror Yarn 1.21.4.  Fabric's registry mixins
    // shadow these members directly, so aliases with different names are not
    // sufficient here.
    private final RegistryKey<?> key;
    private final Lifecycle lifecycle;
    private final boolean intrusive;
    /** Fabric Registry Sync's alias map; initialized with the registry object. */
    private final Map<net.minecraft.util.Identifier, net.minecraft.util.Identifier> aliases = new HashMap<>();
    private final Map<net.minecraft.util.Identifier, T> values = new LinkedHashMap<>();
    private final Map<net.minecraft.util.Identifier, T> registeredValues = new LinkedHashMap<>();
    private final Map<T, net.minecraft.util.Identifier> reverse = new HashMap<>();
    private final Map<net.minecraft.registry.tag.TagKey<T>, Set<T>> tags = new HashMap<>();
    private final Map<RegistryKey<T>, RegistryEntry.Reference<T>> keyToEntry = new LinkedHashMap<>();
    private final Map<net.minecraft.util.Identifier, RegistryEntry.Reference<T>> idToEntry = new LinkedHashMap<>();
    private final Map<T, RegistryEntry.Reference<T>> valueToEntry = new HashMap<>();
    private final Map<T, RegistryEntry.Reference<T>> intrusiveValueToEntry = new HashMap<>();
    private final Map<RegistryKey<T>, RegistryEntryInfo> keyToEntryInfo = new LinkedHashMap<>();
    private final Reference2IntMap<T> entryToRawId = new Reference2IntOpenHashMap<>();
    private final ObjectList<RegistryEntry.Reference<T>> rawIdToEntry = new ObjectArrayList<>();
    private final TagLookup tagLookup = new TagLookup();
    private boolean frozen;
    private final List<Registry.EntryAddedListener<T>> entryListeners = new ArrayList<>();
    private int nativeRegistrySizeCache = -1;
    private List<net.minecraft.util.Identifier> nativeRegistryIdsCache = List.of();

    public SimpleRegistry(RegistryKey<?> key, Lifecycle lifecycle, boolean intrusive) {
        this.key = key;
        this.lifecycle = lifecycle == null ? Lifecycle.stable() : lifecycle;
        this.intrusive = intrusive;
    }
    public SimpleRegistry(RegistryKey<?> key) { this(key, Lifecycle.stable(), false); }

    @Override public RegistryKey<?> getKey() { return key; }
    public Lifecycle getLifecycle() { return lifecycle; }
    public boolean isIntrusive() { return intrusive; }

    /** Concrete overload required by Fabric's 1.21.4 registry mixins. */
    @Override public synchronized T get(RegistryKey<T> entryKey) {
        return entryKey == null ? null : get(entryKey.getValue());
    }

    /** Concrete overload used by Registry Sync's variable injection. */
    public synchronized Optional<T> getOptional(RegistryKey<T> entryKey) {
        return Optional.ofNullable(get(entryKey));
    }

    /** Returns the stable holder for a key, creating an unbound holder when needed. */
    public synchronized RegistryEntry.Reference<T> getOrCreateEntry(RegistryKey<T> entryKey) {
        if (entryKey == null) throw new NullPointerException("key");
        RegistryEntry.Reference<T> existing = keyToEntry.get(entryKey);
        if (existing != null) return existing;
        RegistryEntry.Reference<T> created = new RegistryEntry.Reference<>(get(entryKey), entryKey);
        keyToEntry.put(entryKey, created);
        keyToEntryInfo.put(entryKey, RegistryEntryInfo.DEFAULT);
        return created;
    }

    /** Returns the metadata associated with a registry key, if it is bound. */
    public synchronized Optional<RegistryEntryInfo> getEntryInfo(RegistryKey<T> entryKey) {
        return Optional.ofNullable(keyToEntryInfo.get(entryKey));
    }

    /** Concrete overload used by Registry Sync's contains injection. */
    @Override public synchronized boolean contains(RegistryKey<T> entryKey) {
        return entryKey != null && (keyToEntry.containsKey(entryKey) || get(entryKey) != null);
    }

    @Override public synchronized T get(net.minecraft.util.Identifier id) {
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

    @Override public synchronized Optional<RegistryEntry<T>> getEntry(net.minecraft.util.Identifier id) {
        T value = get(id);
        return value == null ? Optional.empty() : Optional.of(entry(id, value));
    }
    @Override public synchronized Optional<RegistryEntry<T>> getEntry(T value) {
        net.minecraft.util.Identifier id = getId(value);
        return id == null ? Optional.empty() : Optional.of(entry(id, value));
    }
    @Override public synchronized Optional<RegistryEntry<T>> getEntry(int rawId) {
        if (rawId < 0) return Optional.empty();
        List<net.minecraft.util.Identifier> nativeIds = nativeRegistryIdsInRawOrder();
        if (rawId < nativeIds.size()) {
            net.minecraft.util.Identifier id = nativeIds.get(rawId);
            return id == null ? Optional.empty() : getEntry(id);
        }
        int registeredIndex = rawId - nativeIds.size();
        for (Map.Entry<net.minecraft.util.Identifier, T> registered : registeredValues.entrySet()) {
            if (registeredIndex-- == 0)
                return Optional.of(entry(registered.getKey(), registered.getValue()));
        }
        return Optional.empty();
    }

    @Override public synchronized net.minecraft.util.Identifier getId(T value) {
        if (value == null) return null;
        net.minecraft.util.Identifier id = reverse.get(value);
        if (id != null) return id;
        id = identifierOf(value);
        if (id != null && (registeredValues.containsKey(id) || nativeRawId(id) >= 0)) {
            reverse.put(value, id);
            return id;
        }
        for (Map.Entry<net.minecraft.util.Identifier, T> entry : values.entrySet()) {
            if (entry.getValue() == value || entry.getValue().equals(value)) return entry.getKey();
        }
        return null;
    }
    @Override public synchronized int getRawId(T value) {
        net.minecraft.util.Identifier id = getId(value);
        if (id == null) return -1;
        int nativeRawId = nativeRawId(id);
        if (nativeRawId >= 0) return nativeRawId;
        int registeredIndex = 0;
        for (net.minecraft.util.Identifier registeredId : registeredValues.keySet()) {
            if (registeredId.equals(id))
                return nativeRegistryIdsInRawOrder().size() + registeredIndex;
            registeredIndex++;
        }
        return -1;
    }
    @Override public synchronized int size() {
        return nativeRegistryIdsInRawOrder().size() + registeredValues.size();
    }

    @Override public synchronized Set<net.minecraft.util.Identifier> getIds() {
        LinkedHashSet<net.minecraft.util.Identifier> result = new LinkedHashSet<>();
        for (net.minecraft.util.Identifier id : nativeRegistryIdsInRawOrder())
            if (id != null) result.add(id);
        result.addAll(registeredValues.keySet());
        return Collections.unmodifiableSet(result);
    }
    @Override public synchronized Set<Map.Entry<net.minecraft.util.Identifier, T>> entrySet() {
        LinkedHashSet<Map.Entry<net.minecraft.util.Identifier, T>> result = new LinkedHashSet<>();
        for (net.minecraft.util.Identifier id : getIds()) {
            T value = get(id);
            if (value != null)
                result.add(new java.util.AbstractMap.SimpleImmutableEntry<>(id, value));
        }
        return Collections.unmodifiableSet(result);
    }
    @Override public synchronized Stream<T> stream() { return valuesInRawOrder().stream(); }
    @Override public synchronized Iterator<T> iterator() { return valuesInRawOrder().iterator(); }
    @Override public synchronized boolean containsId(net.minecraft.util.Identifier id) { return get(id) != null; }

    @Override public synchronized void registerValue(net.minecraft.util.Identifier id, T value) {
        if (id == null || value == null) throw new NullPointerException("id/value");
        if (registeredValues.containsKey(id) || nativeRawId(id) >= 0)
            throw new IllegalArgumentException("duplicate registry id: " + id);
        int rawId = nativeRegistryIdsInRawOrder().size() + registeredValues.size();
        values.put(id, value);
        registeredValues.put(id, value);
        reverse.put(value, id);
        @SuppressWarnings("unchecked") RegistryKey<T> valueKey = (RegistryKey<T>) RegistryKey.of(key, id);
        RegistryEntry.Reference<T> reference = new RegistryEntry.Reference<>(value, valueKey);
        keyToEntry.put(valueKey, reference);
        idToEntry.put(id, reference);
        valueToEntry.put(value, reference);
        keyToEntryInfo.put(valueKey, RegistryEntryInfo.DEFAULT);
        while (rawIdToEntry.size() < rawId) rawIdToEntry.add(null);
        rawIdToEntry.add(reference);
        entryToRawId.put(value, rawId);
        List<Registry.EntryAddedListener<T>> listeners = List.copyOf(entryListeners);
        for (Registry.EntryAddedListener<T> listener : listeners) listener.onEntryAdded(rawId, id, value);
    }

    /** Concrete target for the 1.21.4 Registry Sync injection point. */
    @Override public synchronized RegistryEntry.Reference<T> add(RegistryKey<T> entryKey,
                                                                  T value,
                                                                  RegistryEntryInfo info) {
        if (entryKey == null) throw new NullPointerException("key");
        registerValue(entryKey.getValue(), value);
        RegistryEntry.Reference<T> reference = idToEntry.get(entryKey.getValue());
        keyToEntryInfo.put(entryKey, info == null ? RegistryEntryInfo.DEFAULT : info);
        return reference == null ? new RegistryEntry.Reference<>(value, entryKey) : reference;
    }
    @Override public synchronized void addTag(net.minecraft.registry.tag.TagKey<T> tag, T value) {
        if (tag != null && value != null) tags.computeIfAbsent(tag, ignored -> new LinkedHashSet<>()).add(value);
    }
    @Override public synchronized boolean hasTag(net.minecraft.registry.tag.TagKey<T> tag, T value) {
        return tag != null && value != null && tags.getOrDefault(tag, Set.of()).contains(value);
    }
    @Override public synchronized void addEntryListener(Registry.EntryAddedListener<T> listener) {
        if (listener != null) entryListeners.add(listener);
    }
    @Override public synchronized boolean removeEntryListener(Registry.EntryAddedListener<T> listener) {
        return entryListeners.remove(listener);
    }
    @Override public boolean ownerEquals(net.minecraft.registry.entry.RegistryEntryOwner<T> other) {
        return this == other;
    }

    /** Accessor target used by Fabric Registry Sync. */
    public boolean isFrozen() { return frozen; }

    /** Yarn's nested tag lookup holder; unbound tags are valid during bootstrap. */
    public static final class TagLookup {
        public TagLookup() { }
        public boolean isBound() { return false; }
        public java.util.Optional<Object> getOptional(net.minecraft.registry.tag.TagKey<?> key) {
            return java.util.Optional.empty();
        }
    }

    private RegistryEntry<T> entry(net.minecraft.util.Identifier id, T value) {
        RegistryEntry.Reference<T> existing = idToEntry.get(id);
        if (existing != null) return existing;
        return new RegistryEntry.Reference<>(value, RegistryKey.of(key, id));
    }
    private String nativeRegistryName() {
        if (key == null || key.getValue() == null) return "";
        return key.getValue().toString();
    }
    private int nativeSize() {
        return switch (nativeRegistryName()) {
            case "minecraft:item", "minecraft:block",
                 "minecraft:entity_type", "minecraft:fluid" ->
                Math.max(0, NativeAccess.registryEntryCount(nativeRegistryName()));
            default -> 0;
        };
    }
    private List<net.minecraft.util.Identifier> nativeRegistryIdsInRawOrder() {
        int count = nativeSize();
        if (nativeRegistrySizeCache == count) return nativeRegistryIdsCache;
        List<net.minecraft.util.Identifier> ids = new ArrayList<>(count);
        for (int rawId = 0; rawId < count; rawId++) {
            ids.add(net.minecraft.util.Identifier.tryParse(
                NativeAccess.registryEntryName(nativeRegistryName(), rawId)));
        }
        nativeRegistryIdsCache = Collections.unmodifiableList(ids);
        nativeRegistrySizeCache = count;
        return nativeRegistryIdsCache;
    }
    private int nativeRawId(net.minecraft.util.Identifier id) {
        if (id == null) return -1;
        List<net.minecraft.util.Identifier> ids = nativeRegistryIdsInRawOrder();
        for (int rawId = 0; rawId < ids.size(); rawId++)
            if (id.equals(ids.get(rawId))) return rawId;
        return -1;
    }
    private List<T> valuesInRawOrder() {
        List<T> result = new ArrayList<>(size());
        for (net.minecraft.util.Identifier id : getIds()) {
            T value = get(id);
            if (value != null) result.add(value);
        }
        return result;
    }
    private static net.minecraft.util.Identifier identifierOf(Object value) {
        if (value instanceof Block block) return block.getId();
        if (value instanceof Item item) return item.getId();
        if (value instanceof EntityType<?> entityType) return entityType.getId();
        if (value instanceof Fluid fluid) return fluid.getId();
        return null;
    }
    @SuppressWarnings("unchecked")
    private T resolveNative(net.minecraft.util.Identifier id) {
        return switch (nativeRegistryName()) {
            case "minecraft:item" -> {
                int rawId = NativeAccess.registryItemId(id.toString());
                yield rawId < 0 ? null : (T) new Item(id, rawId);
            }
            case "minecraft:block" -> {
                int rawState = NativeAccess.registryBlockState(id.toString());
                yield rawState < 0 ? null : (T) new Block(rawState, id);
            }
            case "minecraft:entity_type" -> hasNativeEntry(id)
                ? (T) new EntityType<>(id, () -> null, 0.6f, 1.8f) : null;
            case "minecraft:fluid" -> hasNativeEntry(id) ? (T) new Fluid(id) : null;
            default -> null;
        };
    }
    private boolean hasNativeEntry(net.minecraft.util.Identifier id) {
        int count = Math.min(nativeSize(), 100_000);
        for (int rawId = 0; rawId < count; rawId++) {
            if (id.equals(net.minecraft.util.Identifier.tryParse(
                    NativeAccess.registryEntryName(nativeRegistryName(), rawId)))) return true;
        }
        return false;
    }
}
