package net.minecraft.resource;

import java.util.function.Consumer;

/** Provider hook used by the named resource-pack manager API. */
@FunctionalInterface
public interface ResourcePackProvider {
    void register(Consumer<ResourcePackProfile> profileAdder);
}
