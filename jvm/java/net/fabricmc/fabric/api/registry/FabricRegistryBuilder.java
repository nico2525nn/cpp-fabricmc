package net.fabricmc.fabric.api.registry;

import java.util.EnumSet;
import net.minecraft.registry.Registry;
import net.minecraft.registry.RegistryKey;

/** Dependency-free equivalent of FabricRegistryBuilder for Java-side registries. */
public final class FabricRegistryBuilder<T, R extends Registry<T>> {
    private final RegistryKey<Registry<T>> key;
    private final Registry<T> source;
    private final EnumSet<RegistryAttribute> attributes = EnumSet.noneOf(RegistryAttribute.class);
    private FabricRegistryBuilder(RegistryKey<Registry<T>> key) { this(key, null); }
    private FabricRegistryBuilder(RegistryKey<Registry<T>> key, Registry<T> source) { this.key = key; this.source = source; }
    public static <T> FabricRegistryBuilder<T, Registry<T>> createSimple(RegistryKey<Registry<T>> key) { return new FabricRegistryBuilder<>(key); }
    @SuppressWarnings("unchecked") public static <T> FabricRegistryBuilder<T, Registry<T>> from(Registry<T> registry) {
        if (registry == null) throw new NullPointerException("registry");
        return new FabricRegistryBuilder<>((RegistryKey<Registry<T>>) (RegistryKey<?>) registry.getKey(), registry);
    }
    public FabricRegistryBuilder<T, R> attribute(RegistryAttribute attribute) { if (attribute != null) attributes.add(attribute); return this; }
    public FabricRegistryBuilder<T, R> attribute(RegistryAttribute first, RegistryAttribute... rest) { attribute(first); if (rest != null) for (RegistryAttribute value : rest) attribute(value); return this; }
    @SuppressWarnings("unchecked") public R buildAndRegister() {
        Registry<T> result = new Registry<>(key);
        if (source != null) for (java.util.Map.Entry<net.minecraft.util.Identifier, T> entry : source.entrySet())
            Registry.register(result, entry.getKey(), entry.getValue());
        if (key != null && !net.minecraft.registry.Registries.ROOT.containsId(key.getValue()))
            Registry.register(net.minecraft.registry.Registries.ROOT, key.getValue(), result);
        return (R) result;
    }
    public EnumSet<RegistryAttribute> getAttributes() { return attributes.isEmpty() ? EnumSet.noneOf(RegistryAttribute.class) : EnumSet.copyOf(attributes); }
}
