package net.fabricmc.loader.api;

import java.util.function.BiConsumer;

/**
 * Process-local object exchange used by Fabric mods to coordinate without a
 * hard dependency on one another. The implementation is supplied by the
 * dependency-free FabricLoader facade.
 */
public interface ObjectShare {
    Object get(String key);
    void whenAvailable(String key, BiConsumer<String, Object> consumer);
    Object put(String key, Object value);
    Object putIfAbsent(String key, Object value);
    Object remove(String key);
}
