package cppfm.loader;

import cppfm.transform.DescriptorResolver;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.jar.JarFile;

/**
 * Version-locked intermediary to Yarn named namespace mappings.
 *
 * <p>The pinned Fabric intermediary artifact only describes the official to
 * intermediary namespace.  The compatibility shadow classes in this
 * project are named, so mod bytecode compiled against intermediary needs the
 * second, Yarn supplied, edge before it can be defined by the JVM.  This
 * class reads the Tiny v2 mapping published by Yarn and also implements the
 * resolver used by the structural Mixin transformer.</p>
 *
 * <p>No network access happens here.  Provisioning is performed by
 * {@code tools/fetch_yarn_mappings.py}; the loader accepts an explicit path
 * through {@code CPPFM_INTERMEDIARY_MAPPINGS} or
 * {@code -Dcppfm.intermediary-mappings}.  A checked-in project cache is also
 * discovered relative to the working directory and the loader URLs.</p>
 */
public final class IntermediaryNamedMappings implements DescriptorResolver {
    public static final String SOURCE_NAMESPACE = "intermediary";
    public static final String TARGET_NAMESPACE = "named";
    public static final String VERSION = "1.21.4+build.8";
    private static final String MAPPING_ENTRY = "mappings/mappings.tiny";
    private static final String MAPPING_JAR =
        "yarn-1.21.4+build.8-v2.jar";

    private final Map<String, String> classes;
    private final Map<String, String> reverseClasses;
    private final Map<MemberKey, String> methods;
    private final Map<MemberKey, String> fields;
    private final Map<String, String> uniqueMethodNames;
    private final Map<String, String> uniqueFieldNames;
    private final String source;

    private IntermediaryNamedMappings(Map<String, String> classes,
                                      Map<String, String> reverseClasses,
                                      Map<MemberKey, String> methods,
                                      Map<MemberKey, String> fields,
                                      String source) {
        this.classes = immutable(classes);
        this.reverseClasses = immutable(reverseClasses);
        this.methods = Collections.unmodifiableMap(new LinkedHashMap<>(methods));
        this.fields = Collections.unmodifiableMap(new LinkedHashMap<>(fields));
        this.uniqueMethodNames = Collections.unmodifiableMap(uniqueNames(methods));
        this.uniqueFieldNames = Collections.unmodifiableMap(uniqueNames(fields));
        this.source = source == null ? "identity" : source;
    }

    /** Return a no-op resolver for native-only or unprovisioned launches. */
    public static IntermediaryNamedMappings identity() {
        return new IntermediaryNamedMappings(Map.of(), Map.of(), Map.of(), Map.of(), "identity");
    }

    /**
     * Discover the local mapping without downloading anything.
     *
     * <p>An explicitly configured path is fatal when missing or malformed;
     * implicit discovery falls back to identity so a native-only checkout
     * remains usable.</p>
     */
    public static IntermediaryNamedMappings discover(URL[] urls) {
        String configured = configuredPath();
        if (configured != null) {
            Path path = Path.of(configured).toAbsolutePath().normalize();
            if (!Files.exists(path)) {
                throw new IllegalStateException("configured intermediary/named mapping does not exist: " + path);
            }
            return load(path);
        }

        List<Path> candidates = new ArrayList<>();
        addDefaultCandidates(candidates, Path.of(System.getProperty("user.dir", ".")));
        if (urls != null) {
            for (URL url : urls) {
                if (!"file".equalsIgnoreCase(url.getProtocol())) continue;
                try {
                    Path path = Path.of(url.toURI()).toAbsolutePath().normalize();
                    addDefaultCandidates(candidates, path);
                    if (Files.isRegularFile(path) && path.toString().endsWith(".jar"))
                        candidates.add(path);
                } catch (Exception ignored) {
                    // An unrelated or non-file URL is not a mapping error.
                }
            }
        }
        String runtime = System.getProperty("cppfm.fabric-runtime");
        if (runtime == null || runtime.isBlank()) runtime = System.getenv("CPPFM_FABRIC_RUNTIME");
        if (runtime != null && !runtime.isBlank())
            addDefaultCandidates(candidates, Path.of(runtime).toAbsolutePath().normalize());

        Set<Path> seen = new HashSet<>();
        for (Path candidate : candidates) {
            Path normalized = candidate.toAbsolutePath().normalize();
            if (!seen.add(normalized) || !Files.exists(normalized)) continue;
            try {
                IntermediaryNamedMappings mapping = load(normalized);
                System.out.println("CPPFM_MAPPING_SELECTED namespace=intermediary->named source="
                                   + mapping.source + " classes=" + mapping.classCount()
                                   + " methods=" + mapping.methodCount()
                                   + " fields=" + mapping.fieldCount());
                return mapping;
            } catch (IllegalArgumentException | IllegalStateException ignored) {
                // A candidate can be a normal mod/runtime jar.  Only a file
                // that contains a Tiny mapping is accepted.
            }
        }
        return identity();
    }

    /** Load a Tiny v2 file, a Yarn mapping jar, or a directory containing one. */
    public static IntermediaryNamedMappings load(Path path) {
        try {
            if (Files.isDirectory(path)) {
                Path nested = path.resolve(MAPPING_ENTRY);
                if (!Files.isRegularFile(nested)) nested = path.resolve("mappings.tiny");
                if (!Files.isRegularFile(nested))
                    throw new IOException("mapping file not found below " + path);
                try (InputStream input = Files.newInputStream(nested)) {
                    return parse(input, nested.toString());
                }
            }
            if (!Files.isRegularFile(path)) throw new IOException("mapping path is not a regular file: " + path);
            if (path.toString().endsWith(".jar")) {
                try (JarFile jar = new JarFile(path.toFile())) {
                    var entry = jar.getJarEntry(MAPPING_ENTRY);
                    if (entry == null) throw new IOException("mapping jar has no " + MAPPING_ENTRY + ": " + path);
                    try (InputStream input = jar.getInputStream(entry)) {
                        return parse(input, path.toString() + "!" + MAPPING_ENTRY);
                    }
                }
            }
            try (InputStream input = Files.newInputStream(path)) {
                return parse(input, path.toString());
            }
        } catch (IOException failure) {
            throw new IllegalStateException("cannot load intermediary/named mapping " + path, failure);
        }
    }

    public String source() { return source; }
    public int classCount() { return classes.size(); }
    public int methodCount() { return methods.size(); }
    public int fieldCount() { return fields.size(); }
    public boolean isIdentity() { return classes.isEmpty() && methods.isEmpty() && fields.isEmpty(); }

    /** Map one JVM internal class name from intermediary to named. */
    public String mapClassName(String name) {
        if (name == null || name.isEmpty()) return name;
        if (name.charAt(0) == '[') return mapDescriptor(name);
        boolean dotted = name.indexOf('/') < 0 && name.indexOf('.') >= 0;
        String internal = dotted ? name.replace('.', '/') : name;
        String mapped = classes.get(internal);
        if (mapped == null) return name;
        return dotted ? mapped.replace('/', '.') : mapped;
    }

    /** Map a JVM descriptor containing intermediary class names. */
    public String mapDescriptor(String descriptor) {
        return remapDescriptor(descriptor, classes);
    }

    /** Reverse-map a named JVM descriptor for member-table lookup. */
    public String unmapDescriptor(String descriptor) {
        return remapDescriptor(descriptor, reverseClasses);
    }

    /** Map a field reference using its intermediary owner and descriptor. */
    public String mapFieldName(String owner, String name, String descriptor) {
        return fields.getOrDefault(new MemberKey(owner, name, descriptor), name);
    }

    /** Map a method reference using its intermediary owner and descriptor. */
    public String mapMethodName(String owner, String name, String descriptor) {
        return methods.getOrDefault(new MemberKey(owner, name, descriptor), name);
    }

    /** Map short intermediary symbols used by Mixin annotation strings. */
    public String mapSymbol(String value) {
        if (value == null || value.isEmpty()) return value;
        String className = mapClassName(value);
        if (!className.equals(value)) return className;
        String mapped = uniqueMethodNames.get(value);
        if (mapped != null) return mapped;
        mapped = uniqueFieldNames.get(value);
        if (mapped != null) return mapped;
        if (looksLikeDescriptor(value)) return mapDescriptor(value);
        return remapEmbeddedSymbols(value);
    }

    /** Number of unique intermediary symbols that can be mapped globally. */
    public int uniqueMethodNameCount() { return uniqueMethodNames.size(); }
    public int uniqueFieldNameCount() { return uniqueFieldNames.size(); }

    /** Resolver used after class bytes have been converted to named. */
    @Override
    public String resolveOwner(String owner) {
        return mapClassName(owner);
    }

    @Override
    public String resolveMethod(String owner, String name, String descriptor) {
        String sourceOwner = sourceOwner(owner);
        String sourceDescriptor = isNamedOwner(owner) && descriptor != null
            ? unmapDescriptor(descriptor) : descriptor;
        return mapMethodName(sourceOwner, name, sourceDescriptor);
    }

    @Override
    public String resolveField(String owner, String name, String descriptor) {
        String sourceOwner = sourceOwner(owner);
        String sourceDescriptor = isNamedOwner(owner) && descriptor != null
            ? unmapDescriptor(descriptor) : descriptor;
        return mapFieldName(sourceOwner, name, sourceDescriptor);
    }

    @Override
    public String toString() {
        return "IntermediaryNamedMappings{" + source + ", classes=" + classCount()
            + ", methods=" + methodCount() + ", fields=" + fieldCount() + '}';
    }

    private String sourceOwner(String owner) {
        if (owner == null) return null;
        if (classes.containsKey(owner)) return owner;
        return reverseClasses.getOrDefault(owner, owner);
    }

    private boolean isNamedOwner(String owner) {
        return owner != null && reverseClasses.containsKey(owner) && !classes.containsKey(owner);
    }

    private String remapEmbeddedSymbols(String value) {
        StringBuilder output = new StringBuilder(value.length() + 16);
        int index = 0;
        while (index < value.length()) {
            char current = value.charAt(index);
            if (current == 'L') {
                int end = value.indexOf(';', index + 1);
                if (end > index) {
                    String owner = value.substring(index + 1, end);
                    output.append('L').append(mapClassName(owner)).append(';');
                    index = end + 1;
                    continue;
                }
            }
            if (isSymbolStart(current)) {
                int end = index + 1;
                while (end < value.length() && isSymbolPart(value.charAt(end))) ++end;
                String token = value.substring(index, end);
                String mapped = uniqueMethodNames.get(token);
                if (mapped == null) mapped = uniqueFieldNames.get(token);
                output.append(mapped == null ? token : mapped);
                index = end;
                continue;
            }
            output.append(current);
            ++index;
        }
        return output.toString();
    }

    private static boolean isSymbolStart(char value) {
        return value == 'm' || value == 'f' || value == 'c';
    }

    private static boolean isSymbolPart(char value) {
        return Character.isLetterOrDigit(value) || value == '_' || value == '$';
    }

    private static boolean looksLikeDescriptor(String value) {
        return value.charAt(0) == 'L' || value.charAt(0) == '[' || value.charAt(0) == '(';
    }

    private static String remapDescriptor(String descriptor, Map<String, String> mapping) {
        if (descriptor == null || descriptor.isEmpty()) return descriptor;
        StringBuilder output = null;
        int index = 0;
        while (index < descriptor.length()) {
            int start = descriptor.indexOf('L', index);
            if (start < 0) break;
            int end = descriptor.indexOf(';', start + 1);
            if (end < 0) break;
            String owner = descriptor.substring(start + 1, end);
            String mapped = mapping.get(owner);
            if (mapped != null) {
                if (output == null) output = new StringBuilder(descriptor.length() + 16);
                output.append(descriptor, index, start + 1).append(mapped).append(';');
                index = end + 1;
            } else {
                if (output != null) output.append(descriptor, index, end + 1);
                index = end + 1;
            }
        }
        if (output == null) return descriptor;
        if (index < descriptor.length()) output.append(descriptor, index, descriptor.length());
        return output.toString();
    }

    private static String configuredPath() {
        for (String property : List.of("cppfm.intermediary-mappings", "cppfm.mapping", "cppfm.yarn-mappings")) {
            String value = System.getProperty(property);
            if (value != null && !value.isBlank()) return value.trim();
        }
        for (String variable : List.of("CPPFM_INTERMEDIARY_MAPPINGS", "CPPFM_MAPPING", "CPPFM_YARN_MAPPINGS")) {
            String value = System.getenv(variable);
            if (value != null && !value.isBlank()) return value.trim();
        }
        return null;
    }

    private static void addDefaultCandidates(List<Path> output, Path location) {
        Path root = Files.isDirectory(location) ? location : location.getParent();
        for (int depth = 0; root != null && depth < 7; ++depth, root = root.getParent()) {
            Path cache = root.resolve("build/fabric-runtime/net/fabricmc/yarn/1.21.4+build.8");
            output.add(cache.resolve(MAPPING_JAR));
            output.add(cache.resolve(MAPPING_ENTRY));
            output.add(root.resolve("jvm/vendor/" + MAPPING_JAR));
        }
    }

    private static IntermediaryNamedMappings parse(InputStream input, String source) throws IOException {
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(input, StandardCharsets.UTF_8))) {
            String header = reader.readLine();
            if (header == null) throw new IOException("empty Tiny mapping");
            String[] columns = header.split("\\t", -1);
            if (columns.length < 5 || !"tiny".equals(columns[0]) || !"2".equals(columns[1]))
                throw new IOException("expected Tiny v2 mapping header with intermediary and named namespaces");
            int sourceNamespace = -1;
            int targetNamespace = -1;
            for (int index = 3; index < columns.length; ++index) {
                if (SOURCE_NAMESPACE.equals(columns[index])) sourceNamespace = index - 3;
                if (TARGET_NAMESPACE.equals(columns[index])) targetNamespace = index - 3;
            }
            if (sourceNamespace < 0 || targetNamespace < 0 || sourceNamespace == targetNamespace)
                throw new IOException("Tiny mapping must expose intermediary -> named namespaces");

            LinkedHashMap<String, String> classes = new LinkedHashMap<>();
            LinkedHashMap<String, String> reverseClasses = new LinkedHashMap<>();
            LinkedHashMap<MemberKey, String> methods = new LinkedHashMap<>();
            LinkedHashMap<MemberKey, String> fields = new LinkedHashMap<>();
            String currentClass = null;
            for (String line; (line = reader.readLine()) != null; ) {
                if (line.isEmpty()) continue;
                String[] parts = line.split("\\t", -1);
                if (parts.length >= 3 && "c".equals(parts[0])) {
                    String from = column(parts, 1 + sourceNamespace);
                    String to = column(parts, 1 + targetNamespace);
                    if (!from.isEmpty() && !to.isEmpty()) {
                        classes.put(from, to);
                        reverseClasses.put(to, from);
                    }
                    currentClass = from.isEmpty() ? null : from;
                } else if (parts.length >= 5 && parts[0].isEmpty() && currentClass != null
                           && ("m".equals(parts[1]) || "f".equals(parts[1]))) {
                    String descriptor = parts[2];
                    String from = column(parts, 3 + sourceNamespace);
                    String to = column(parts, 3 + targetNamespace);
                    if (from.isEmpty() || to.isEmpty()) continue;
                    MemberKey key = new MemberKey(currentClass, from, descriptor);
                    if ("m".equals(parts[1])) methods.put(key, to);
                    else fields.put(key, to);
                }
            }
            if (classes.isEmpty()) throw new IOException("Tiny mapping contains no classes");
            return new IntermediaryNamedMappings(classes, reverseClasses, methods, fields, source);
        }
    }

    private static String column(String[] columns, int index) {
        return index >= 0 && index < columns.length ? columns[index] : "";
    }

    private static Map<String, String> uniqueNames(Map<MemberKey, String> mappings) {
        HashMap<String, String> output = new HashMap<>();
        Set<String> ambiguous = new HashSet<>();
        for (Map.Entry<MemberKey, String> entry : mappings.entrySet()) {
            String sourceName = entry.getKey().name;
            String previous = output.putIfAbsent(sourceName, entry.getValue());
            if (previous != null && !previous.equals(entry.getValue())) ambiguous.add(sourceName);
        }
        for (String name : ambiguous) output.remove(name);
        return output;
    }

    private static <K, V> Map<K, V> immutable(Map<K, V> source) {
        return Collections.unmodifiableMap(new LinkedHashMap<>(source));
    }

    private record MemberKey(String owner, String name, String descriptor) { }
}
