package net.minecraft.resource;

import java.io.BufferedReader;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import net.minecraft.registry.VersionedIdentifier;
import net.minecraft.resource.metadata.ResourceMetadata;

/** Closeable binary resource returned by a resource manager. */
public class Resource implements AutoCloseable {
    private final ResourcePack pack;
    private final net.minecraft.resource.InputSupplier<InputStream> inputSupplier;
    private final net.minecraft.resource.InputSupplier<ResourceMetadata> metadataSupplier;
    private final List<InputStream> openStreams = new ArrayList<>();
    private ResourceMetadata metadata;
    private boolean closed;

    /**
     * Legacy nested spelling retained for source compatibility with earlier
     * cpp-fabricmc stubs.  The official 1.21.4 type is the top-level generic
     * {@link net.minecraft.resource.InputSupplier}.
     */
    @Deprecated
    @FunctionalInterface
    public interface InputSupplier extends net.minecraft.resource.InputSupplier<InputStream> { }

    public Resource(ResourcePack pack,
                    net.minecraft.resource.InputSupplier<InputStream> inputSupplier) {
        this(pack, inputSupplier, ResourceMetadata.NONE_SUPPLIER);
    }

    public Resource(ResourcePack pack,
                    net.minecraft.resource.InputSupplier<InputStream> inputSupplier,
                    net.minecraft.resource.InputSupplier<ResourceMetadata> metadataSupplier) {
        this.pack = pack;
        this.inputSupplier = Objects.requireNonNull(inputSupplier, "inputSupplier");
        this.metadataSupplier = Objects.requireNonNull(metadataSupplier, "metadataSupplier");
    }

    public ResourcePack getPack() { return pack; }

    /** Compatibility alias used by older shadow callers. */
    public ResourcePack getResourcePack() { return getPack(); }

    public String getPackId() { return pack == null ? "" : pack.getId(); }

    public Optional<VersionedIdentifier> getKnownPackInfo() {
        return pack == null ? Optional.empty() : pack.getKnownPackInfo();
    }

    public synchronized InputStream getInputStream() throws IOException {
        if (closed) throw new IOException("resource is closed");
        InputStream stream = Objects.requireNonNull(inputSupplier.get(), "inputSupplier returned null");
        openStreams.add(stream);
        return stream;
    }

    public BufferedReader getReader() throws IOException {
        return new BufferedReader(new InputStreamReader(getInputStream(), StandardCharsets.UTF_8));
    }

    public synchronized ResourceMetadata getMetadata() throws IOException {
        if (metadata == null) metadata = Objects.requireNonNull(metadataSupplier.get(), "metadataSupplier returned null");
        return metadata;
    }

    public static Resource ofBytes(byte[] bytes) {
        byte[] copy = bytes == null ? new byte[0] : bytes.clone();
        return new Resource(null, () -> new ByteArrayInputStream(copy));
    }

    @Override
    public synchronized void close() {
        if (closed) return;
        closed = true;
        IOException failure = null;
        for (InputStream stream : openStreams) {
            try { stream.close(); }
            catch (IOException error) { if (failure == null) failure = error; }
        }
        openStreams.clear();
        // ResourcePack ownership belongs to the resource manager, not to an
        // individual Resource view.  A manager may return several Resource
        // objects backed by the same pack; closing one of them must not make
        // the remaining views unusable.  LifecycledResourceManager.close()
        // closes the packs exactly once after all views are no longer used.
        if (failure != null) throw new ResourceCloseException(failure);
    }

    /** Closeable.close cannot declare a checked exception in this ABI. */
    private static final class ResourceCloseException extends RuntimeException {
        private ResourceCloseException(IOException cause) { super(cause); }
    }
}
