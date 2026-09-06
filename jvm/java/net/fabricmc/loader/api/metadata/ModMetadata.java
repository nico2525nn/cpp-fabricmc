package net.fabricmc.loader.api.metadata;

import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.Optional;

import net.fabricmc.loader.api.Version;

/**
 * Fabric Loader 0.16.x metadata surface.  Defaults keep the generated shadow
 * ABI useful for small mods while the fallback loader fills the identity,
 * version, environment and provides fields for resolved candidates.
 */
public interface ModMetadata {
    default String getType() { return "regular"; }
    String getId();
    Collection<String> getProvides();
    Version getVersion();
    default ModEnvironment getEnvironment() { return ModEnvironment.UNIVERSAL; }
    default Collection<ModDependency> getDependencies() { return List.of(); }
    default Collection<ModDependency> getDepends() { return getDependencies(); }
    default Collection<ModDependency> getRecommends() { return List.of(); }
    default Collection<ModDependency> getSuggests() { return List.of(); }
    default Collection<ModDependency> getConflicts() { return List.of(); }
    default Collection<ModDependency> getBreaks() { return List.of(); }
    String getName();
    default String getDescription() { return ""; }
    default Collection<Person> getAuthors() { return List.of(); }
    default Collection<Person> getContributors() { return List.of(); }
    default ContactInformation getContact() { return ContactInformation.EMPTY; }
    default Collection<String> getLicense() { return List.of(); }
    default Optional<String> getIconPath(int size) { return Optional.empty(); }
    default boolean containsCustomValue(String key) { return getCustomValues().containsKey(key); }
    default CustomValue getCustomValue(String key) { return getCustomValues().get(key); }
    default Map<String, CustomValue> getCustomValues() { return Map.of(); }
    default boolean containsCustomElement(String key) { return containsCustomValue(key); }
}
