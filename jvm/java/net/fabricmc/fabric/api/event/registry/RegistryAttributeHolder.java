package net.fabricmc.fabric.api.event.registry;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.registry.Registry;
import net.minecraft.registry.RegistryKey;

/** Per-registry attribute set used by registry sync. */
public interface RegistryAttributeHolder {
    Map<Object, RegistryAttributeHolderImpl> HOLDERS = new ConcurrentHashMap<>();

    static RegistryAttributeHolder get(RegistryKey<?> key) {
        return HOLDERS.computeIfAbsent(key, ignored -> new RegistryAttributeHolderImpl());
    }
    static RegistryAttributeHolder get(Registry<?> registry) {
        return HOLDERS.computeIfAbsent(registry, ignored -> new RegistryAttributeHolderImpl());
    }

    RegistryAttributeHolder addAttribute(RegistryAttribute attribute);
    boolean hasAttribute(RegistryAttribute attribute);

    final class RegistryAttributeHolderImpl implements RegistryAttributeHolder {
        private final java.util.Set<RegistryAttribute> attributes =
            java.util.EnumSet.noneOf(RegistryAttribute.class);
        @Override public synchronized RegistryAttributeHolder addAttribute(RegistryAttribute attribute) {
            if (attribute != null) attributes.add(attribute);
            return this;
        }
        @Override public synchronized boolean hasAttribute(RegistryAttribute attribute) {
            return attribute != null && attributes.contains(attribute);
        }
    }
}
