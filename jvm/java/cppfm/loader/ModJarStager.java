package cppfm.loader;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.FileVisitResult;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

/**
 * Stages Fabric mods into the named namespace used by cppfm's Minecraft ABI.
 *
 * <p>Published Fabric mods are generally compiled against intermediary names,
 * while cppfm's Java shadow API is Yarn-named.  This makes a private copy of
 * the configured mods directory and transforms class files before Knot
 * discovers them.  Original mod files are never modified.</p>
 */
public final class ModJarStager {
    private static final int MAX_NESTED_JAR_DEPTH = 16;
    private static final Limits DEFAULT_LIMITS = new Limits(
        2L * 1024 * 1024 * 1024,
        256L * 1024 * 1024,
        32L * 1024 * 1024,
        8L * 1024 * 1024,
        512L * 1024 * 1024,
        1_000_000,
        100_000);

    /** Stage all top-level Fabric jar and directory mods in deterministic order. */
    public static Result stage(Path source, Path destination,
                               IntermediaryNamedMappings mappings) throws IOException {
        return stage(source, destination, mappings, DEFAULT_LIMITS);
    }

    /**
     * Stage mods with explicit decompression and entry-count budgets. The
     * default overload is deliberately bounded; this overload lets operators
     * raise limits for trusted, unusually large mod collections.
     */
    public static Result stage(Path source, Path destination,
                               IntermediaryNamedMappings mappings,
                               Limits limits) throws IOException {
        if (source == null || destination == null || mappings == null)
            throw new NullPointerException("mod staging arguments");
        if (limits == null) throw new NullPointerException("staging limits");
        if (mappings.isIdentity())
            throw new IOException("intermediary-to-named mappings are unavailable; refusing to stage mods unchanged");
        if (!Files.isDirectory(source))
            throw new IOException("mods directory does not exist: " + source);

        Path sourceRoot = source.toRealPath();
        Path requestedTarget = destination.toAbsolutePath().normalize();
        if (requestedTarget.startsWith(sourceRoot))
            throw new IOException("staged mods directory must not be inside the original mods directory");
        Path targetRoot = resolveWithoutCreating(requestedTarget);
        if (targetRoot.startsWith(sourceRoot))
            throw new IOException("staged mods directory must not resolve inside the original mods directory");
        if (Files.exists(targetRoot, LinkOption.NOFOLLOW_LINKS))
            throw new IOException("staged mods destination already exists: " + targetRoot);

        ModJarStager stager = new ModJarStager(mappings, limits);
        List<Path> candidates;
        try (var entries = Files.list(sourceRoot)) {
            candidates = new ArrayList<>();
            var iterator = entries.iterator();
            while (iterator.hasNext()) {
                if (candidates.size() >= limits.maxTotalEntries())
                    throw new IOException("mods directory exceeds top-level entry limit: " + sourceRoot);
                candidates.add(iterator.next());
            }
        }
        candidates.sort(Comparator.comparing(path -> path.getFileName().toString()));
        Path parent = targetRoot.getParent();
        if (parent == null)
            throw new IOException("staged mods destination has no parent: " + targetRoot);
        Files.createDirectories(parent);
        Files.createDirectory(targetRoot);
        try {
            targetRoot = targetRoot.toRealPath();
            if (targetRoot.startsWith(sourceRoot))
                throw new IOException("staged mods directory must not resolve inside the original mods directory");
            for (Path candidate : candidates) {
                if (Files.isSymbolicLink(candidate))
                    throw new IOException("symbolic links are not supported in the mods directory: " + candidate);
                Path output = targetRoot.resolve(candidate.getFileName().toString());
                if (Files.isDirectory(candidate)) {
                    stager.copyDirectory(candidate, output);
                    stager.stats.modCount++;
                } else if (Files.isRegularFile(candidate) && candidate.getFileName().toString().endsWith(".jar")) {
                    stager.remapArchive(candidate, output, 0);
                    stager.stats.modCount++;
                }
            }
            return stager.stats.snapshot();
        } catch (IOException | RuntimeException | Error failure) {
            try {
                deleteTree(targetRoot);
            } catch (IOException cleanupFailure) {
                failure.addSuppressed(cleanupFailure);
            }
            throw failure;
        }
    }

    public record Limits(long maxTotalExpandedBytes, long maxEntryBytes,
                         long maxClassBytes, long maxAccessWidenerBytes,
                         long maxNestedJarBytes, long maxTotalEntries,
                         int maxEntriesPerContainer) {
        public Limits {
            if (maxTotalExpandedBytes <= 0 || maxEntryBytes <= 0 ||
                maxClassBytes <= 0 || maxAccessWidenerBytes <= 0 ||
                maxNestedJarBytes <= 0 || maxTotalEntries <= 0 ||
                maxEntriesPerContainer <= 0 ||
                maxClassBytes > Integer.MAX_VALUE ||
                maxAccessWidenerBytes > Integer.MAX_VALUE)
                throw new IllegalArgumentException(
                    "staging limits must be positive and in-memory entry limits must fit in a Java array");
        }
    }

    /** Resolve existing symlinks before creating the target, without touching it. */
    private static Path resolveWithoutCreating(Path path) throws IOException {
        List<Path> missingNames = new ArrayList<>();
        Path existing = path;
        while (existing != null &&
               !Files.exists(existing, LinkOption.NOFOLLOW_LINKS)) {
            Path name = existing.getFileName();
            if (name == null)
                throw new IOException("cannot resolve staged mods destination: " + path);
            missingNames.add(name);
            existing = existing.getParent();
        }
        if (existing == null)
            throw new IOException("staged mods destination has no existing ancestor: " + path);
        Path resolved = existing.toRealPath();
        for (int index = missingNames.size() - 1; index >= 0; --index)
            resolved = resolved.resolve(missingNames.get(index));
        return resolved.normalize();
    }

    private final ClassFileNamespaceRemapper remapper;
    private final MutableStats stats = new MutableStats();
    private final Limits limits;

    private ModJarStager(IntermediaryNamedMappings mappings, Limits limits) {
        remapper = new ClassFileNamespaceRemapper(mappings);
        this.limits = limits;
    }

    private void copyDirectory(Path source, Path destination) throws IOException {
        Files.createDirectories(destination);
        List<Path> paths = new ArrayList<>();
        try (var entries = Files.walk(source)) {
            var iterator = entries.iterator();
            while (iterator.hasNext()) {
                Path path = iterator.next();
                if (path.equals(source)) continue;
                if (paths.size() >= limits.maxEntriesPerContainer())
                    throw new IOException("mod directory exceeds entry limit: " + source);
                paths.add(path);
            }
        }
        paths.sort(Comparator.comparing(path -> source.relativize(path).toString()));
        for (Path path : paths) {
            recordEntry(path.toString());
            if (Files.isSymbolicLink(path))
                throw new IOException("symbolic links are not supported in a mod directory: " + path);
            Path output = destination.resolve(source.relativize(path));
            if (Files.isDirectory(path)) {
                Files.createDirectories(output);
            } else if (Files.isRegularFile(path)) {
                Files.createDirectories(output.getParent());
                if (path.getFileName().toString().endsWith(".class")) {
                    byte[] classBytes;
                    try (InputStream input = Files.newInputStream(path)) {
                        classBytes = readBounded(input, limits.maxClassBytes(), path.toString());
                    }
                    Files.write(output, remapClass(path.toString(), classBytes));
                } else if (path.getFileName().toString().endsWith(".accesswidener")) {
                    byte[] widener;
                    try (InputStream input = Files.newInputStream(path)) {
                        widener = readBounded(input, limits.maxAccessWidenerBytes(),
                                              path.toString());
                    }
                    Files.write(output, AccessWidenerNamespaceRemapper.remap(
                        widener, remapper.mappings()));
                } else if (path.getFileName().toString().endsWith(".jar")) {
                    remapArchive(path, output, 0);
                } else {
                    try (InputStream input = Files.newInputStream(path);
                         OutputStream copy = Files.newOutputStream(output)) {
                        copyBounded(input, copy, limits.maxEntryBytes(), path.toString());
                    }
                    stats.resourceCount++;
                }
            } else {
                throw new IOException("unsupported filesystem entry in mod directory: " + path);
            }
        }
    }

    private void remapArchive(Path source, Path destination, int depth) throws IOException {
        if (depth > MAX_NESTED_JAR_DEPTH)
            throw new IOException("nested mod jar depth exceeds " + MAX_NESTED_JAR_DEPTH + ": " + source);
        if (Files.size(source) > limits.maxTotalExpandedBytes())
            throw new IOException("mod archive exceeds input-size limit: " + source);
        Files.createDirectories(destination.getParent());
        try (ZipFile input = new ZipFile(source.toFile());
             OutputStream fileOutput = Files.newOutputStream(destination);
             ZipOutputStream output = new ZipOutputStream(fileOutput)) {
            if (input.getComment() != null) output.setComment(input.getComment());
            Enumeration<? extends ZipEntry> enumeration = input.entries();
            List<ZipEntry> entries = new ArrayList<>();
            while (enumeration.hasMoreElements()) {
                if (entries.size() >= limits.maxEntriesPerContainer())
                    throw new IOException("mod archive exceeds entry limit: " + source);
                entries.add(enumeration.nextElement());
            }
            entries.sort(Comparator.comparing(ZipEntry::getName));
            Set<String> names = new HashSet<>();
            for (ZipEntry entry : entries) {
                String name = entry.getName();
                recordEntry(source + "!/" + name);
                if (!names.add(name)) throw new IOException("duplicate jar entry " + name + " in " + source);
                if (isSignatureFile(name)) continue;
                ZipEntry staged = new ZipEntry(name);
                if (entry.getTime() >= 0) staged.setTime(entry.getTime());
                output.putNextEntry(staged);
                if (!entry.isDirectory()) {
                    try (InputStream data = input.getInputStream(entry)) {
                        if (name.endsWith(".class")) {
                            byte[] classBytes = readBounded(data, limits.maxClassBytes(),
                                                           source + "!/" + name);
                            output.write(remapClass(name, classBytes));
                        } else if (name.endsWith(".accesswidener")) {
                            byte[] widener = readBounded(data,
                                limits.maxAccessWidenerBytes(), source + "!/" + name);
                            output.write(AccessWidenerNamespaceRemapper.remap(
                                widener, remapper.mappings()));
                        } else if (name.endsWith(".jar")) {
                            remapNestedJar(data, output, depth + 1, source + "!/" + name);
                        } else {
                            copyBounded(data, output, limits.maxEntryBytes(),
                                        source + "!/" + name);
                            stats.resourceCount++;
                        }
                    }
                }
                output.closeEntry();
            }
        }
    }

    private void remapNestedJar(InputStream source, ZipOutputStream destination,
                                int depth, String label) throws IOException {
        Path input = null;
        Path output = null;
        Throwable failure = null;
        try {
            input = Files.createTempFile("cppfm-mod-remap-input-", ".jar");
            output = Files.createTempFile("cppfm-mod-remap-output-", ".jar");
            try (OutputStream temp = Files.newOutputStream(input)) {
                copyBounded(source, temp, limits.maxNestedJarBytes(), label, false);
            }
            remapArchive(input, output, depth);
            try (InputStream remapped = Files.newInputStream(output)) {
                copyBounded(remapped, destination, limits.maxNestedJarBytes(), label, false);
            }
        } catch (IOException problem) {
            failure = new IOException(
                "cannot stage nested mod jar " + label + ": " + problem.getMessage(), problem);
            throw (IOException) failure;
        } catch (RuntimeException | Error problem) {
            failure = problem;
            throw problem;
        } finally {
            IOException cleanupFailure = deleteTemporaryFile(output, null);
            cleanupFailure = deleteTemporaryFile(input, cleanupFailure);
            if (cleanupFailure != null) {
                if (failure != null) failure.addSuppressed(cleanupFailure);
                else throw cleanupFailure;
            }
        }
    }

    private static IOException deleteTemporaryFile(Path path, IOException priorFailure) {
        if (path == null) return priorFailure;
        try {
            Files.deleteIfExists(path);
        } catch (IOException cleanupFailure) {
            if (priorFailure == null) return cleanupFailure;
            priorFailure.addSuppressed(cleanupFailure);
        }
        return priorFailure;
    }

    private byte[] readBounded(InputStream input, long entryLimit, String label)
        throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        copyBounded(input, bytes, entryLimit, label);
        return bytes.toByteArray();
    }

    private void copyBounded(InputStream input, OutputStream output,
                             long entryLimit, String label) throws IOException {
        copyBounded(input, output, entryLimit, label, true);
    }

    private void copyBounded(InputStream input, OutputStream output, long entryLimit,
                             String label, boolean accountExpandedBytes) throws IOException {
        byte[] buffer = new byte[32 * 1024];
        long entryBytes = 0;
        int read;
        while ((read = input.read(buffer)) != -1) {
            long count = read;
            if (count > entryLimit - entryBytes)
                throw new IOException("mod entry exceeds expanded-size limit: " + label);
            if (accountExpandedBytes &&
                count > limits.maxTotalExpandedBytes() - stats.expandedBytes)
                throw new IOException("mod set exceeds total expanded-size limit at " + label);
            output.write(buffer, 0, read);
            entryBytes += count;
            if (accountExpandedBytes) stats.expandedBytes += count;
        }
    }

    private void recordEntry(String label) throws IOException {
        if (stats.entryCount >= limits.maxTotalEntries())
            throw new IOException("mod set exceeds total entry-count limit at " + label);
        stats.entryCount++;
    }

    private static void deleteTree(Path root) throws IOException {
        if (!Files.exists(root, LinkOption.NOFOLLOW_LINKS)) return;
        Files.walkFileTree(root, new SimpleFileVisitor<>() {
            @Override
            public FileVisitResult visitFile(Path file, BasicFileAttributes attributes)
                throws IOException {
                Files.delete(file);
                return FileVisitResult.CONTINUE;
            }

            @Override
            public FileVisitResult postVisitDirectory(Path directory, IOException failure)
                throws IOException {
                if (failure != null) throw failure;
                Files.delete(directory);
                return FileVisitResult.CONTINUE;
            }
        });
    }

    private byte[] remapClass(String entryName, byte[] original) {
        String internalName = entryName;
        int versioned = internalName.indexOf("/classes/");
        if (internalName.startsWith("META-INF/versions/") && versioned >= 0)
            internalName = internalName.substring(versioned + "/classes/".length());
        if (internalName.endsWith(".class"))
            internalName = internalName.substring(0, internalName.length() - ".class".length());
        String binaryName = internalName.replace('/', '.');
        stats.classCount++;
        return ClassFileNamespaceRemapper.hasMixinClassAnnotation(original)
            ? remapper.remapMixin(binaryName, original)
            : remapper.remap(binaryName, original);
    }

    private static boolean isSignatureFile(String name) {
        String upper = name.toUpperCase(java.util.Locale.ROOT);
        if (!upper.startsWith("META-INF/")) return false;
        int separator = upper.lastIndexOf('/');
        if (separator != "META-INF".length()) return false;
        String leaf = upper.substring(separator + 1);
        return leaf.endsWith(".SF") || leaf.endsWith(".RSA") || leaf.endsWith(".DSA")
            || leaf.endsWith(".EC") || leaf.startsWith("SIG-");
    }

    private static final class MutableStats {
        private int modCount;
        private int classCount;
        private int resourceCount;
        private long expandedBytes;
        private long entryCount;

        Result snapshot() { return new Result(modCount, classCount, resourceCount); }
    }

    public record Result(int modCount, int classCount, int resourceCount) { }
}
