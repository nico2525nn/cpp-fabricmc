package net.minecraft.resource;

import java.io.FileNotFoundException;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.TreeMap;
import java.util.function.Predicate;
import java.util.stream.Stream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

import net.minecraft.util.Identifier;

/**
 * A bounded, closeable resource manager for the embedded server.
 *
 * <p>Packs are ordered from lowest to highest priority. A lookup returns all
 * matching resources in that order, while the singular lookup returns the
 * highest-priority resource. The manager intentionally covers only ordinary
 * directory and ZIP packs; pack profiles, built-in pack activation, and client
 * resource loading remain outside this bridge.</p>
 */
public class LifecycledResourceManager implements ResourceManager, AutoCloseable {
    private final ResourceType type;
    private final List<ResourcePack> packs;
    private boolean closed;

    /** Creates an empty server-data manager for compatibility callers. */
    public LifecycledResourceManager() {
        this(ResourceType.SERVER_DATA, List.of());
    }

    public LifecycledResourceManager(Collection<? extends ResourcePack> packs) {
        this(ResourceType.SERVER_DATA, packs);
    }

    public LifecycledResourceManager(ResourceType type,
                                     Collection<? extends ResourcePack> packs) {
        this.type = Objects.requireNonNull(type, "type");
        List<ResourcePack> copy = new ArrayList<>();
        if (packs != null) {
            for (ResourcePack pack : packs)
                if (pack != null) copy.add(pack);
        }
        this.packs = Collections.unmodifiableList(copy);
    }

    /** Opens the supplied pack roots in the order in which they are given. */
    public static LifecycledResourceManager fromPaths(ResourceType type,
                                                       Collection<Path> roots) {
        List<ResourcePack> packs = new ArrayList<>();
        if (roots != null) {
            int index = 0;
            for (Path root : roots) {
                if (root == null) continue;
                Path normalized = root.toAbsolutePath().normalize();
                try {
                    if (Files.isDirectory(normalized, LinkOption.NOFOLLOW_LINKS))
                        packs.add(new DirectoryResourcePack("path-" + index, normalized));
                    else if (Files.isRegularFile(normalized, LinkOption.NOFOLLOW_LINKS))
                        packs.add(new ZipResourcePack("path-" + index, normalized));
                } catch (InvalidPathException ignored) {
                    // A stale optional pack must not prevent the server from
                    // constructing the manager for the remaining roots.
                }
                index++;
            }
        }
        return new LifecycledResourceManager(type, packs);
    }

    public ResourceType getType() { return type; }

    public List<ResourcePack> getPacks() { return packs; }

    @Override
    public synchronized java.util.Optional<Resource> getResource(Identifier id) {
        List<Resource> resources = getAllResources(id);
        return resources.isEmpty()
            ? java.util.Optional.empty()
            : java.util.Optional.of(resources.get(resources.size() - 1));
    }

    @Override
    public synchronized List<Resource> getAllResources(Identifier id) {
        if (closed || id == null) return List.of();
        List<Resource> resources = new ArrayList<>();
        for (ResourcePack pack : packs) {
            InputSupplier<InputStream> supplier = pack.open(type, id);
            if (supplier != null) resources.add(new Resource(pack, supplier));
        }
        return List.copyOf(resources);
    }

    @Override
    public synchronized Map<Identifier, Resource> findResources(
            String startingPath, Predicate<String> allowedPathPredicate) {
        if (closed) return Map.of();
        TreeMap<Identifier, Resource> result = new TreeMap<>();
        find(startingPath, allowedPathPredicate, (id, supplier) ->
            result.put(id, new Resource(currentPack, supplier)));
        return Collections.unmodifiableMap(new LinkedHashMap<>(result));
    }

    @Override
    public synchronized Map<Identifier, List<Resource>> findAllResources(
            String startingPath, Predicate<String> allowedPathPredicate) {
        if (closed) return Map.of();
        TreeMap<Identifier, List<Resource>> found = new TreeMap<>();
        find(startingPath, allowedPathPredicate, (id, supplier) ->
            found.computeIfAbsent(id, ignored -> new ArrayList<>())
                .add(new Resource(currentPack, supplier)));
        LinkedHashMap<Identifier, List<Resource>> result = new LinkedHashMap<>();
        for (Map.Entry<Identifier, List<Resource>> entry : found.entrySet())
            result.put(entry.getKey(), List.copyOf(entry.getValue()));
        return Collections.unmodifiableMap(result);
    }

    /* ResourcePack.ResultConsumer has no pack parameter, so this field is set
       only while a pack callback is executing under the manager lock. */
    private ResourcePack currentPack;

    private void find(String startingPath, Predicate<String> predicate,
                      ResourcePack.ResultConsumer consumer) {
        String prefix = normalizePrefix(startingPath);
        Predicate<String> allowed = predicate == null ? ignored -> true : predicate;
        for (ResourcePack pack : packs) {
            currentPack = pack;
            try {
                for (String namespace : pack.getNamespaces(type)) {
                    if (namespace == null || namespace.isEmpty()) continue;
                    pack.findResources(type, namespace, prefix, (id, supplier) -> {
                        if (id != null && supplier != null && allowed.test(id.getPath()))
                            consumer.accept(id, supplier);
                    });
                }
            } finally {
                currentPack = null;
            }
        }
    }

    @Override
    public synchronized Set<String> getAllNamespaces() {
        if (closed) return Set.of();
        LinkedHashSet<String> result = new LinkedHashSet<>();
        for (ResourcePack pack : packs) result.addAll(pack.getNamespaces(type));
        return Collections.unmodifiableSet(result);
    }

    @Override
    public synchronized Stream<ResourcePack> streamResourcePacks() {
        return packs.stream();
    }

    @Override
    public synchronized void close() {
        if (closed) return;
        closed = true;
        RuntimeException failure = null;
        for (ResourcePack pack : packs) {
            try { pack.close(); }
            catch (RuntimeException error) {
                if (failure == null) failure = error;
                else failure.addSuppressed(error);
            }
        }
        if (failure != null) throw failure;
    }

    private static String normalizePrefix(String value) {
        if (value == null || value.isEmpty()) return "";
        String normalized = value.replace('\\', '/');
        while (normalized.startsWith("/")) normalized = normalized.substring(1);
        while (normalized.endsWith("/")) normalized = normalized.substring(0, normalized.length() - 1);
        if (normalized.isEmpty()) return "";
        for (String segment : normalized.split("/"))
            if (segment.equals("..") || segment.isEmpty()) return "";
        return normalized;
    }

    private abstract static class PathResourcePack implements ResourcePack {
        private final String id;
        final Path root;

        PathResourcePack(String id, Path root) {
            this.id = id;
            this.root = root;
        }

        @Override public String getId() { return id; }

        static String entryName(ResourceType type, Identifier id) {
            return type.getDirectory() + "/" + id.getNamespace() + "/" + id.getPath();
        }

        static String prefixName(ResourceType type, String namespace, String prefix) {
            String base = type.getDirectory() + "/" + namespace + "/";
            return prefix == null || prefix.isEmpty() ? base : base + prefix + "/";
        }

        static Identifier identifier(String namespace, String path) {
            if (namespace == null || namespace.isEmpty() || path == null || path.isEmpty()) return null;
            if (path.endsWith(ResourcePack.METADATA_PATH_SUFFIX)) return null;
            return Identifier.tryParse(namespace + ":" + path);
        }
    }

    private static final class DirectoryResourcePack extends PathResourcePack {
        DirectoryResourcePack(String id, Path root) { super(id, root); }

        @Override
        public InputSupplier<InputStream> openRoot(String... segments) {
            Path path = root;
            if (segments != null) for (String segment : segments) {
                if (segment == null || segment.isEmpty() || segment.contains("/")
                        || segment.equals(".") || segment.equals("..")) return null;
                path = path.resolve(segment);
            }
            return fileSupplier(path);
        }

        @Override
        public InputSupplier<InputStream> open(ResourceType type, Identifier id) {
            if (id == null) return null;
            Path path = root.resolve(entryName(type, id)).normalize();
            InputSupplier<InputStream> supplier = fileSupplier(path);
            if (supplier != null) return supplier;
            // cppfm's embedded assets historically use data/<path> for the
            // vanilla namespace. Accept that flat layout as a small bridge
            // compatibility rule while preserving normal namespace lookup.
            if (type == ResourceType.SERVER_DATA && id.getNamespace().equals("minecraft"))
                return fileSupplier(root.resolve("data").resolve(id.getPath()).normalize());
            return null;
        }

        @Override
        public void findResources(ResourceType type, String namespace, String prefix,
                                  ResultConsumer consumer) {
            if (namespace == null || namespace.isEmpty() || consumer == null) return;
            Path base = root.resolve(type.getDirectory()).resolve(namespace).normalize();
            if (!base.startsWith(root) || !Files.isDirectory(base, LinkOption.NOFOLLOW_LINKS)) return;
            try (Stream<Path> stream = Files.walk(base)) {
                stream.filter(path -> Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS))
                    .sorted()
                    .forEach(path -> {
                        String relative = base.relativize(path).toString()
                            .replace(java.io.File.separatorChar, '/');
                        Identifier id = identifier(namespace, relative);
                        if (id != null && (prefix == null || prefix.isEmpty()
                                || relative.equals(prefix) || relative.startsWith(prefix + "/")))
                            consumer.accept(id, InputSupplier.create(path));
                    });
            } catch (IOException error) {
                throw new IllegalStateException("cannot scan resource pack " + root, error);
            }
        }

        @Override
        public Set<String> getNamespaces(ResourceType type) {
            Path base = root.resolve(type.getDirectory());
            if (!Files.isDirectory(base, LinkOption.NOFOLLOW_LINKS)) return Set.of();
            try (Stream<Path> stream = Files.list(base)) {
                return stream.filter(path -> Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS))
                    .map(path -> path.getFileName().toString())
                    .filter(Identifier::isValidNamespace)
                    .collect(java.util.stream.Collectors.toCollection(LinkedHashSet::new));
            } catch (IOException error) {
                throw new IllegalStateException("cannot list resource namespaces " + root, error);
            }
        }

        private static InputSupplier<InputStream> fileSupplier(Path path) {
            if (path == null || !Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)) return null;
            return InputSupplier.create(path);
        }
    }

    private static final class ZipResourcePack extends PathResourcePack {
        ZipResourcePack(String id, Path root) { super(id, root); }

        @Override
        public InputSupplier<InputStream> openRoot(String... segments) {
            StringBuilder name = new StringBuilder();
            if (segments != null) for (String segment : segments) {
                if (segment == null || segment.isEmpty() || segment.contains("/")
                        || segment.equals(".") || segment.equals("..")) return null;
                if (name.length() > 0) name.append('/');
                name.append(segment);
            }
            return zipSupplier(name.toString());
        }

        @Override
        public InputSupplier<InputStream> open(ResourceType type, Identifier id) {
            return id == null ? null : zipSupplier(entryName(type, id));
        }

        @Override
        public void findResources(ResourceType type, String namespace, String prefix,
                                  ResultConsumer consumer) {
            if (namespace == null || namespace.isEmpty() || consumer == null) return;
            String base = prefixName(type, namespace, prefix);
            try (ZipFile zip = new ZipFile(root.toFile())) {
                List<ZipEntry> entries = entries(zip);
                entries.stream().sorted(java.util.Comparator.comparing(ZipEntry::getName))
                    .filter(entry -> !entry.isDirectory())
                    .forEach(entry -> {
                        String name = entry.getName();
                        if (!name.startsWith(base)) return;
                        String relative = name.substring(type.getDirectory().length() + 1
                            + namespace.length() + 1);
                        Identifier id = identifier(namespace, relative);
                        if (id != null) consumer.accept(id, zipSupplier(name));
                    });
            } catch (IOException error) {
                throw new IllegalStateException("cannot scan resource pack " + root, error);
            }
        }

        @Override
        public Set<String> getNamespaces(ResourceType type) {
            String prefix = type.getDirectory() + "/";
            LinkedHashSet<String> result = new LinkedHashSet<>();
            try (ZipFile zip = new ZipFile(root.toFile())) {
                entries(zip).stream()
                    .map(ZipEntry::getName)
                    .filter(name -> name.startsWith(prefix))
                    .map(name -> name.substring(prefix.length()))
                    .map(name -> name.indexOf('/') < 0 ? name : name.substring(0, name.indexOf('/')))
                    .filter(Identifier::isValidNamespace)
                    .forEach(result::add);
            } catch (IOException error) {
                throw new IllegalStateException("cannot list resource namespaces " + root, error);
            }
            return Collections.unmodifiableSet(result);
        }

        private static List<ZipEntry> entries(ZipFile zip) {
            List<ZipEntry> result = new ArrayList<>();
            java.util.Enumeration<? extends ZipEntry> enumeration = zip.entries();
            while (enumeration.hasMoreElements()) result.add(enumeration.nextElement());
            return result;
        }

        private InputSupplier<InputStream> zipSupplier(String name) {
            if (name == null || name.isEmpty()) return null;
            try (ZipFile zip = new ZipFile(root.toFile())) {
                if (zip.getEntry(name) == null) return null;
            } catch (IOException error) {
                throw new IllegalStateException("cannot inspect resource pack " + root, error);
            }
            return () -> {
                ZipFile zip = new ZipFile(root.toFile());
                ZipEntry entry = zip.getEntry(name);
                if (entry == null) {
                    zip.close();
                    throw new FileNotFoundException(name);
                }
                InputStream stream = zip.getInputStream(entry);
                return new FilterInputStream(stream) {
                    @Override public void close() throws IOException {
                        try { super.close(); }
                        finally { zip.close(); }
                    }
                };
            };
        }
    }
}
