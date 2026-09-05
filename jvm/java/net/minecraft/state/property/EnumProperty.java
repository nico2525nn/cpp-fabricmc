package net.minecraft.state.property;

import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import java.util.Locale;
import java.util.Optional;
import net.minecraft.util.StringIdentifiable;

public final class EnumProperty<T extends Enum<T> & Comparable<T>> extends Property<T> {
    private final Class<T> type;
    private final List<T> values;
    private EnumProperty(String name, Class<T> type, Collection<T> values) { super(name); this.type = type; this.values = List.copyOf(values); }
    public static <T extends Enum<T> & Comparable<T>> EnumProperty<T> of(String name, Class<T> type) { return new EnumProperty<>(name, type, Arrays.asList(type.getEnumConstants())); }
    public static <T extends Enum<T> & Comparable<T>> EnumProperty<T> of(String name, Class<T> type, Collection<T> values) { return new EnumProperty<>(name, type, values); }
    @Override public Collection<T> getValues() { return values; }
    @Override public Optional<T> parse(String value) {
        if (value == null) return Optional.empty();
        for (T element : values) if (name(element).equals(value)) return Optional.of(element);
        return Optional.empty();
    }
    @Override public String name(T value) { return value instanceof StringIdentifiable identifiable ? identifiable.asString() : value.name().toLowerCase(Locale.ROOT); }
}
