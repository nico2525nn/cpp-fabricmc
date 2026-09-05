package net.fabricmc.loader.api;

import java.nio.file.Path;
import java.util.List;

public final class ModContainer {
    private final net.fabricmc.loader.api.metadata.ModMetadata metadata;
    private final Path root;
    public ModContainer(net.fabricmc.loader.api.metadata.ModMetadata metadata, Path root) { this.metadata = metadata; this.root = root; }
    public net.fabricmc.loader.api.metadata.ModMetadata getMetadata() { return metadata; }
    public List<Path> getRootPaths() { return root == null ? List.of() : List.of(root); }
    public Path getRootPath() { return root; }
}
