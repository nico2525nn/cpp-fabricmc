package net.minecraft.state;

import java.util.ArrayList;
import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Consumer;
import net.minecraft.state.property.Property;

public final class StateManager<O, S> {
    private final O owner;
    private final List<Property<?>> properties;
    public StateManager(O owner, Consumer<Builder<O, S>> consumer) {
        this.owner = owner;
        Builder<O, S> builder = new Builder<>(owner);
        if (consumer != null) consumer.accept(builder);
        this.properties = List.copyOf(builder.properties);
    }
    public O getOwner() { return owner; }
    public Collection<Property<?>> getProperties() { return properties; }
    public List<S> getStates() { return List.of(); }
    public static final class Builder<O, S> {
        private final O owner;
        private final List<Property<?>> properties = new ArrayList<>();
        private Builder(O owner) { this.owner = owner; }
        public Builder<O, S> add(Property<?>... values) { if (values != null) for (Property<?> value : values) if (value != null && !properties.contains(value)) properties.add(value); return this; }
        public Collection<Property<?>> getProperties() { return List.copyOf(properties); }
        public O getOwner() { return owner; }
    }
}
