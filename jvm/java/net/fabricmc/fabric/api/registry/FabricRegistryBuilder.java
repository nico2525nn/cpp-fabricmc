package net.fabricmc.fabric.api.registry;

import java.util.EnumSet;
import net.minecraft.registry.Registry;
import net.minecraft.registry.RegistryKey;

/** Dependency-free equivalent of FabricRegistryBuilder for Java-side registries. */
public final class FabricRegistryBuilder<T, R extends Registry<T>> {
    private final RegistryKey<Registry<T>> key;
    private final EnumSet<RegistryAttribute> attributes = EnumSet.noneOf(RegistryAttribute.class);
    private FabricRegistryBuilder(RegistryKey<Registry<T>> key) { this.key = key; }
    public static <T> FabricRegistryBuilder<T, Registry<T>> createSimple(RegistryKey<Registry<T>> key) { return new FabricRegistryBuilder<>(key); }
    public static <T> FabricRegistryBuilder<T, Registry<T>> from(Registry<T> registry) { return new FabricRegistryBuilder<>(null); }
    public FabricRegistryBuilder<T, R> attribute(RegistryAttribute attribute) { if (attribute != null) attributes.add(attribute); return this; }
    public FabricRegistryBuilder<T, R> attribute(RegistryAttribute first, RegistryAttribute... rest) { attribute(first); if (rest != null) for (RegistryAttribute value : rest) attribute(value); return this; }
    @SuppressWarnings("unchecked") public R buildAndRegister() { return (R) new Registry<T>(key); }
    public EnumSet<RegistryAttribute> getAttributes() { return attributes.isEmpty() ? EnumSet.noneOf(RegistryAttribute.class) : EnumSet.copyOf(attributes); }
}
