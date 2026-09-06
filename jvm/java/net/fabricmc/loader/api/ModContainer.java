package net.fabricmc.loader.api;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Collection;
import java.util.List;
import java.util.Optional;

import net.fabricmc.loader.api.metadata.ModOrigin;

/** Public Fabric Loader mod-container contract. */
public interface ModContainer {
    net.fabricmc.loader.api.metadata.ModMetadata getMetadata();
    List<Path> getRootPaths();

    default Optional<Path> findPath(String path) {
        if (path == null || path.isBlank()) return Optional.empty();
        for (Path root : getRootPaths()) {
            Path candidate = root.resolve(path).normalize();
            if (Files.exists(candidate)) return Optional.of(candidate);
        }
        return Optional.empty();
    }

    default ModOrigin getOrigin() {
        List<Path> roots = getRootPaths();
        return new ModOrigin() {
            @Override public Kind getKind() { return Kind.PATH; }
            @Override public List<Path> getPaths() { return roots; }
            @Override public String getParentModId() { return null; }
            @Override public String getParentSubLocation() { return null; }
        };
    }

    default Optional<ModContainer> getContainingMod() { return Optional.empty(); }
    default Collection<ModContainer> getContainedMods() { return List.of(); }
    default Path getRoot() { return getRootPaths().stream().findFirst().orElse(null); }
    default Path getRootPath() { return getRoot(); }
    default Path getPath(String path) { return findPath(path).orElse(null); }
}
