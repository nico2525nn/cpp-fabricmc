package net.fabricmc.fabric.api.tag.convention.v1;

import java.util.Objects;
import net.minecraft.registry.Registry;
import net.minecraft.registry.tag.TagKey;

/** Membership helpers for conventional Fabric tags. */
public final class TagUtil {
    private TagUtil() { }
    public static <T> boolean isIn(TagKey<T> tag, T value) {
        return isIn(null, tag, value);
    }
    public static <T> boolean isIn(net.minecraft.registry.DynamicRegistryManager registries, TagKey<T> tag, T value) {
        Objects.requireNonNull(tag, "tag");
        Objects.requireNonNull(value, "value");
        @SuppressWarnings("unchecked")
        Registry<T> registry = (Registry<T>) net.minecraft.registry.Registries.byKey(tag.registry());
        return registry != null && registry.hasTag(tag, value);
    }
}
