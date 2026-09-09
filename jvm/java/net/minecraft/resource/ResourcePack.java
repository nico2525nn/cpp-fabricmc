package net.minecraft.resource;

import java.io.IOException;
import java.io.InputStream;
import java.util.Collections;
import java.util.Optional;
import java.util.Set;
import net.minecraft.registry.VersionedIdentifier;
import net.minecraft.resource.metadata.ResourceMetadataSerializer;
import net.minecraft.util.Identifier;

/** Resource-pack contract used by the 1.21.4 reload and data-pack APIs. */
public interface ResourcePack extends AutoCloseable {
    String METADATA_PATH_SUFFIX = ".mcmeta";
    String PACK_METADATA_NAME = "pack.mcmeta";

    @FunctionalInterface
    interface ResultConsumer {
        void accept(Identifier id, InputSupplier<InputStream> supplier);
    }

    default InputSupplier<InputStream> openRoot(String... segments) { return null; }
    default InputSupplier<InputStream> open(ResourceType type, Identifier id) { return null; }
    default void findResources(ResourceType type, String namespace, String prefix,
                               ResultConsumer consumer) { }
    default Set<String> getNamespaces(ResourceType type) { return Collections.emptySet(); }

    default <T> T parseMetadata(ResourceMetadataSerializer<T> metadataSerializer) throws IOException {
        return null;
    }

    default ResourcePackInfo getInfo() {
        String id = getId();
        return new ResourcePackInfo(id, net.minecraft.text.Text.literal(id),
                ResourcePackSource.NONE, Optional.empty());
    }

    default String getId() { return toString(); }
    default Optional<VersionedIdentifier> getKnownPackInfo() { return Optional.empty(); }
    @Override default void close() { }
}
