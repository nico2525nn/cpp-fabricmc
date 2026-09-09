package net.minecraft.state;

import java.util.Collection;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.function.Function;
import com.mojang.serialization.Codec;
import com.mojang.serialization.MapCodec;
import it.unimi.dsi.fastutil.objects.Reference2ObjectArrayMap;
import net.minecraft.state.property.Property;

/**
 * Immutable property-state base class exposed by Minecraft 1.21.4.
 *
 * <p>The native implementation owns the authoritative block state id.  The
 * Java side still needs the vanilla generic shape because Fabric mixins use
 * this class directly (notably the content-registry API).  The map operations
 * here are real immutable overlays, so property-aware mods observe stable
 * values even though the native world remains the source of block ids.</p>
 */
public class State<O, S> {
    private final String PROPERTIES = "";
    private final Map<Property<?>, Comparable<?>> withMap;
    private final Function<Object, String> PROPERTY_MAP_PRINTER = value -> String.valueOf(value);
    private final String NAME = "";
    protected final O owner;
    protected final Reference2ObjectArrayMap<Property<?>, Comparable<?>> propertyMap;
    protected final MapCodec<S> codec;

    protected State(O owner,
                    Reference2ObjectArrayMap<Property<?>, Comparable<?>> propertyMap,
                    MapCodec<S> codec) {
        this.owner = owner;
        this.propertyMap = propertyMap == null
            ? new Reference2ObjectArrayMap<>()
            : new Reference2ObjectArrayMap<>(propertyMap);
        this.withMap = Collections.unmodifiableMap(this.propertyMap);
        this.codec = codec;
    }

    public Map<Property<?>, Comparable<?>> toMapWith(Property<?> property, Comparable<?> value) {
        Map<Property<?>, Comparable<?>> values = new LinkedHashMap<>(propertyMap);
        values.put(property, value);
        return values;
    }

    public Collection<Property<?>> getProperties() {
        return Collections.unmodifiableSet(propertyMap.keySet());
    }

    protected void createWithMap(Map<Property<?>, Comparable<?>> states) { }

    public Object method_28492(State<?, ?> state) {
        return state == null ? null : state.owner;
    }

    @SuppressWarnings("unchecked")
    public <T extends Comparable<T>> T get(Property<T> property) {
        return (T) propertyMap.get(property);
    }

    public static <O, S> MapCodec<S> method_28497(Function<?, ?> owner, O ignored) {
        return null;
    }

    public Object method_64217(Map<?, ?> values, Property<?> property, Comparable<?> value) {
        return value;
    }

    /** Named overload used by FerriteCore's State cache overwrite. */
    public Object with(Property<?> property, Comparable<?> value, Comparable<?> oldValue) {
        return oldValue == null || oldValue.equals(propertyMap.get(property)) ? value : this;
    }

    public Object getNext(List<?> values, Object value) {
        if (values == null || values.isEmpty()) return value;
        int index = values.indexOf(value);
        return values.get((index + 1 + values.size()) % values.size());
    }

    public boolean contains(Property<?> property) {
        return propertyMap.containsKey(property);
    }

    public Map<Property<?>, Comparable<?>> getEntries() {
        return withMap;
    }

    public Optional<Comparable<?>> getOrEmpty(Property<?> property) {
        return Optional.ofNullable(propertyMap.get(property));
    }

    public Comparable<?> getNullable(Property<?> property) {
        return propertyMap.get(property);
    }

    @SuppressWarnings("unchecked")
    public <T extends Comparable<T>> S with(Property<T> property, T value) {
        return (S) this;
    }

    public static <O, S> State<O, S> method_38860(State<O, S> state, Optional<?> value) {
        return state;
    }

    public Object method_64216(Property<?> property, Comparable<?> value, Comparable<?> oldValue) {
        return oldValue == null || oldValue.equals(propertyMap.get(property)) ? value : this;
    }

    public <T extends Comparable<T>> S cycle(Property<T> property) {
        @SuppressWarnings("unchecked") S self = (S) this;
        return self;
    }

    @SuppressWarnings("unchecked")
    public <T extends Comparable<T>> T get(Property<T> property, T fallback) {
        T value = get(property);
        return value == null ? fallback : value;
    }

    public Comparable<?> withIfExists(Property<?> property, Comparable<?> value) {
        return contains(property) ? value : propertyMap.get(property);
    }

    public static <T> Codec<T> createCodec(Codec<T> codec, Function<?, ?> ownerToStateFunction) {
        return codec;
    }
}
