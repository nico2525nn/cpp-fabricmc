package net.fabricmc.fabric.api.event.registry;

import java.util.EnumSet;
import net.minecraft.registry.DefaultedRegistry;
import net.minecraft.registry.MutableRegistry;
import net.minecraft.registry.Registry;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.SimpleDefaultedRegistry;
import net.minecraft.registry.SimpleRegistry;
import net.minecraft.util.Identifier;
import com.mojang.serialization.Lifecycle;

/** Builder for server-side Fabric registries. */
public final class FabricRegistryBuilder<T, R extends MutableRegistry<T>> {
    private final RegistryKey<Registry<T>> key;
    private final R source;
    private final Identifier defaultId;
    private final EnumSet<RegistryAttribute> attributes = EnumSet.noneOf(RegistryAttribute.class);

    private FabricRegistryBuilder(RegistryKey<Registry<T>> key, R source, Identifier defaultId) {
        this.key = key;
        this.source = source;
        this.defaultId = defaultId;
    }

    @SuppressWarnings("unchecked")
    public static <T, R extends MutableRegistry<T>> FabricRegistryBuilder<T, R> from(R registry) {
        if (registry == null) throw new NullPointerException("registry");
        return new FabricRegistryBuilder<>((RegistryKey<Registry<T>>) (RegistryKey<?>) registry.getKey(),
            registry, registry instanceof DefaultedRegistry<?> defaulted ? defaulted.getDefaultId() : null);
    }

    public static <T> FabricRegistryBuilder<T, MutableRegistry<T>> createSimple(
            RegistryKey<Registry<T>> key) {
        return new FabricRegistryBuilder<>(key, null, null);
    }

    public static <T> FabricRegistryBuilder<T, DefaultedRegistry<T>> createDefaulted(
            RegistryKey<Registry<T>> key, Identifier defaultId) {
        return new FabricRegistryBuilder<>(key, null, defaultId);
    }

    public static <T> FabricRegistryBuilder<T, MutableRegistry<T>> createSimple(
            Class<T> valueClass, Identifier registryId) {
        return createSimple(RegistryKey.of(RegistryKeys.ROOT, registryId));
    }

    public static <T> FabricRegistryBuilder<T, DefaultedRegistry<T>> createDefaulted(
            Class<T> valueClass, Identifier registryId, Identifier defaultId) {
        return createDefaulted(RegistryKey.of(RegistryKeys.ROOT, registryId), defaultId);
    }

    public FabricRegistryBuilder<T, R> attribute(RegistryAttribute attribute) {
        if (attribute != null) attributes.add(attribute);
        return this;
    }

    @SuppressWarnings("unchecked")
    public R buildAndRegister() {
        MutableRegistry<T> result = defaultId == null
            ? new SimpleRegistry<>(key)
            : new SimpleDefaultedRegistry<>(defaultId.toString(), key,
                Lifecycle.stable(), false);
        if (source != null) for (java.util.Map.Entry<Identifier, T> entry : source.entrySet())
            Registry.register(result, entry.getKey(), entry.getValue());
        RegistryAttributeHolder holder = RegistryAttributeHolder.get(result);
        for (RegistryAttribute attribute : attributes) holder.addAttribute(attribute);
        return (R) result;
    }
}
