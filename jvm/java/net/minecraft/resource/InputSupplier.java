package net.minecraft.resource;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Objects;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** Lazily opens a resource stream, matching the 1.21.4 named ABI. */
@FunctionalInterface
public interface InputSupplier<T> {
    T get() throws IOException;

    static InputSupplier<InputStream> create(Path path) {
        Objects.requireNonNull(path, "path");
        return () -> Files.newInputStream(path);
    }

    static InputSupplier<InputStream> create(ZipFile zipFile, ZipEntry zipEntry) {
        Objects.requireNonNull(zipFile, "zipFile");
        Objects.requireNonNull(zipEntry, "zipEntry");
        return () -> zipFile.getInputStream(zipEntry);
    }
}
