package net.minecraft.registry.entry;

import com.mojang.serialization.Lifecycle;
import java.util.Optional;

/**
 * Registry metadata carried by the 1.21.4 mutable-registry API.
 *
 * <p>Known-pack metadata is intentionally represented as an erased optional:
 * the embedded runtime does not ship the data-pack implementation, while the
 * JVM descriptor and accessor methods remain compatible with Fabric.</p>
 */
public final class RegistryEntryInfo {
    public static final RegistryEntryInfo DEFAULT = new RegistryEntryInfo(
        Optional.empty(), Lifecycle.stable());

    private final Optional<?> knownPackInfo;
    private final Lifecycle lifecycle;

    public RegistryEntryInfo(Optional<?> knownPackInfo, Lifecycle lifecycle) {
        this.knownPackInfo = knownPackInfo == null ? Optional.empty() : knownPackInfo;
        this.lifecycle = lifecycle == null ? Lifecycle.stable() : lifecycle;
    }

    public RegistryEntryInfo(Lifecycle lifecycle, Optional<?> knownPackInfo) {
        this(knownPackInfo, lifecycle);
    }

    public Optional<?> knownPackInfo() { return knownPackInfo; }
    public Lifecycle lifecycle() { return lifecycle; }
}
