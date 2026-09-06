package net.fabricmc.loader.api;

import java.util.Optional;

/** Semantic version extension used by Minecraft and common Fabric mods. */
public interface SemanticVersion extends Version {
    int COMPONENT_WILDCARD = -1;
    default int getVersionComponentCount() { return 3; }
    int getVersionComponent(int component);
    default Optional<String> getPrereleaseKey() { return Optional.empty(); }
    default Optional<String> getBuildKey() { return Optional.empty(); }
    default boolean hasWildcard() { return false; }
    default boolean isPreRelease() { return false; }

    default int compareTo(SemanticVersion other) {
        return Version.super.compareTo(other);
    }

    static SemanticVersion parse(String value) throws VersionParsingException {
        Version parsed = Version.parse(value);
        return (SemanticVersion) parsed;
    }
}
