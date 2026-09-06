package net.minecraft.entity.attribute;

import java.util.LinkedHashMap;
import java.util.Map;

/** Minimal attribute container ABI for vanilla entity factory methods. */
public class DefaultAttributeContainer {
    private final Map<Object, Object> instances;
    public DefaultAttributeContainer() { this(new LinkedHashMap<>()); }
    public DefaultAttributeContainer(Map<Object, Object> instances) {
        this.instances = instances == null ? new LinkedHashMap<>() : new LinkedHashMap<>(instances);
    }
    public double getValue(Object attribute) { return 0.0D; }
    public double getBaseValue(Object attribute) { return 0.0D; }
    public boolean has(Object attribute) { return instances.containsKey(attribute); }
    public static Builder builder() { return new Builder(); }

    public static class Builder {
        private final Map<Object, Object> instances = new LinkedHashMap<>();
        public Builder add(Object attribute, double baseValue) { instances.put(attribute, baseValue); return this; }
        public Builder add(Object attribute) { return add(attribute, 0.0D); }
        public DefaultAttributeContainer build() { return new DefaultAttributeContainer(instances); }
    }
}
