package net.minecraft.registry;

import java.util.Optional;

/** Small registry context type used by data/resource condition APIs. */
public class RegistryOps {
    public interface RegistryInfoGetter {
        default Optional<?> getRegistryInfo(RegistryKey<?> key) { return Optional.empty(); }
    }
}
