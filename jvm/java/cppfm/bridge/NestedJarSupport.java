package cppfm.bridge;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.jar.JarEntry;
import java.util.jar.JarFile;

/**
 * Materializes Fabric's {@code META-INF/jars/*.jar} entries for the fallback
 * loader. Fabric API, C2ME, ServerCore, Spark, and many other real mods use
 * nested jars for their module and library graph; treating only the outer
 * archive as a class-path entry makes those distributions fail before their
 * metadata can be resolved.
 *
 * <p>The official Fabric Loader path owns its own nested-jar handling. This
 * helper is only used by the compatibility loader and the fallback
 * Knot-shaped class loader. Extracted files live in a private temporary tree
 * and are removed after the JVM runtime shuts down.</p>
 */
final class NestedJarSupport {
    private static final String PREFIX = "META-INF/jars/";
    private static final long MAX_JAR_SIZE = 128L * 1024L * 1024L;
    private static final long MAX_TOTAL_SIZE = 512L * 1024L * 1024L;
    private static final int MAX_NESTED_JARS = 1024;
    private static final int MAX_DEPTH = 4;

    private NestedJarSupport() {}

    static Expansion expand(List<Path> roots) throws IOException {
        Path temporaryRoot = Files.createTempDirectory("cppfm-nested-jars-");
        List<Path> all = new ArrayList<>(roots);
        ArrayDeque<Pending> pending = new ArrayDeque<>();
        for (Path root : roots) {
            if (isJar(root)) pending.addLast(new Pending(root, 0));
        }
        Set<Path> inspected = new HashSet<>();
        long totalBytes = 0;
        int extractedCount = 0;
        try {
            while (!pending.isEmpty()) {
                Pending current = pending.removeFirst();
                Path source = current.path.toAbsolutePath().normalize();
                if (!inspected.add(source)) continue;
                if (current.depth >= MAX_DEPTH) continue;
                try (JarFile jar = new JarFile(source.toFile())) {
                    List<JarEntry> entries = jar.stream()
                        .filter(entry -> !entry.isDirectory()
                            && entry.getName().startsWith(PREFIX)
                            && entry.getName().endsWith(".jar"))
                        .sorted(Comparator.comparing(JarEntry::getName))
                        .toList();
                    for (JarEntry entry : entries) {
                        if (++extractedCount > MAX_NESTED_JARS)
                            throw new IOException("too many nested JARs (limit " + MAX_NESTED_JARS + ")");
                        long declaredSize = entry.getSize();
                        if (declaredSize < 0 || declaredSize > MAX_JAR_SIZE)
                            throw new IOException("nested JAR exceeds size limit: " + entry.getName());
                        if (totalBytes > MAX_TOTAL_SIZE - declaredSize)
                            throw new IOException("nested JAR set exceeds size limit");
                        String relativeName = entry.getName().substring(PREFIX.length());
                        if (relativeName.contains("/") || relativeName.contains("\\")
                            || relativeName.contains(".."))
                            throw new IOException("unsafe nested JAR name: " + entry.getName());
                        String fileName = Path.of(relativeName).getFileName().toString();
                        if (fileName.isBlank() || fileName.equals(".") || fileName.equals(".."))
                            throw new IOException("unsafe nested JAR name: " + entry.getName());
                        Path target = temporaryRoot.resolve(String.format(
                            "%04d-%s", extractedCount, fileName));
                        try (InputStream input = jar.getInputStream(entry);
                             OutputStream output = Files.newOutputStream(target)) {
                            copyBounded(input, output, declaredSize, entry.getName());
                        }
                        totalBytes += declaredSize;
                        all.add(target);
                        pending.addLast(new Pending(target, current.depth + 1));
                    }
                }
            }
            return new Expansion(List.copyOf(all), temporaryRoot);
        } catch (IOException failure) {
            deleteTree(temporaryRoot);
            throw failure;
        }
    }

    private static void copyBounded(InputStream input, OutputStream output,
                                    long expected, String name) throws IOException {
        byte[] buffer = new byte[8192];
        long copied = 0;
        int read;
        while ((read = input.read(buffer)) >= 0) {
            if (read == 0) continue;
            copied += read;
            if (copied > expected || copied > MAX_JAR_SIZE)
                throw new IOException("nested JAR size mismatch or overflow: " + name);
            output.write(buffer, 0, read);
        }
        if (copied != expected)
            throw new IOException("nested JAR size mismatch: " + name);
    }

    private static boolean isJar(Path path) {
        return Files.isRegularFile(path) && path.getFileName().toString().endsWith(".jar");
    }

    private static void deleteTree(Path root) {
        if (root == null || !Files.exists(root)) return;
        try (var stream = Files.walk(root)) {
            stream.sorted(Comparator.reverseOrder()).forEach(path -> {
                try { Files.deleteIfExists(path); }
                catch (IOException ignored) { /* cleanup is best effort */ }
            });
        } catch (IOException ignored) { /* cleanup is best effort */ }
    }

    static final class Expansion {
        private final List<Path> paths;
        private final Path temporaryRoot;
        private boolean closed;

        private Expansion(List<Path> paths, Path temporaryRoot) {
            this.paths = paths;
            this.temporaryRoot = temporaryRoot;
        }

        List<Path> paths() { return paths; }

        void close() {
            if (closed) return;
            closed = true;
            deleteTree(temporaryRoot);
        }
    }

    private record Pending(Path path, int depth) {}
}
