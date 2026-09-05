package net.minecraft.state.property;

import java.util.Collection;
import java.util.Optional;
import java.util.Objects;

public abstract class Property<T extends Comparable<T>> implements Comparable<Property<?>> {
    private final String name;
    protected Property(String name) { this.name = Objects.requireNonNull(name, "name"); }
    public String getName() { return name; }
    public abstract Collection<T> getValues();
    public abstract Optional<T> parse(String value);
    public String name(T value) { return value == null ? "" : value.toString(); }
    @Override public int compareTo(Property<?> other) { return name.compareTo(other == null ? "" : other.name); }
    @Override public boolean equals(Object other) { return other instanceof Property<?> property && getClass() == property.getClass() && name.equals(property.name); }
    @Override public int hashCode() { return Objects.hash(getClass(), name); }
    @Override public String toString() { return name; }
}
