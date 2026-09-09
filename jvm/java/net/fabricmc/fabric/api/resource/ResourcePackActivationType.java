package net.fabricmc.fabric.api.resource;

/**
 * Activation policy for a resource pack supplied by a Fabric mod.
 *
 * <p>This is a top-level type in Fabric API 0.119.x.  Keeping it separate
 * from {@link ResourceManagerHelper} is important because the type appears in
 * the public method descriptors used by compiled mods.</p>
 */
public enum ResourcePackActivationType {
    NORMAL(false),
    DEFAULT_ENABLED(true),
    ALWAYS_ENABLED(true);

    private final boolean enabledByDefault;

    ResourcePackActivationType(boolean enabledByDefault) {
        this.enabledByDefault = enabledByDefault;
    }

    public boolean isEnabledByDefault() { return enabledByDefault; }
}
