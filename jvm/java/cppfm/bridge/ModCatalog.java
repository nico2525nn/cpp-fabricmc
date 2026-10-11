package cppfm.bridge;

import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.BiPredicate;
import java.util.jar.JarFile;

/** Reads Fabric mod metadata and produces a validated dependency order. */
final class ModCatalog {
    private static final Set<String> BUILTIN_DEPENDENCIES =
        Set.of("fabricloader", "minecraft", "java");
    private static final Map<String, String> BUILTIN_VERSIONS = Map.of(
        "fabricloader", "0.16.9",
        "minecraft", "1.21.4",
        "java", javaMajorVersion());

    private ModCatalog() { }

    static List<Path> roots(Path directory) throws java.io.IOException {
        try (var stream = Files.list(directory)) {
            return stream
                .filter(path -> Files.isDirectory(path) || path.toString().endsWith(".jar"))
                .sorted(Comparator.comparing(Path::toString))
                .toList();
        }
    }

    static Candidate read(Path path) throws Exception {
        String text;
        if (Files.isDirectory(path)) {
            Path metadata = path.resolve("fabric.mod.json");
            if (!Files.isRegularFile(metadata)) return null;
            text = Files.readString(metadata);
        } else {
            try (JarFile jar = new JarFile(path.toFile())) {
                var entry = jar.getJarEntry("fabric.mod.json");
                if (entry == null) return null;
                try (InputStream input = jar.getInputStream(entry)) {
                    text = new String(input.readAllBytes(), StandardCharsets.UTF_8);
                }
            }
        }
        Object parsed = MiniJson.parse(text);
        if (!(parsed instanceof Map<?, ?> raw))
            throw new IllegalArgumentException("fabric.mod.json is not an object");
        @SuppressWarnings("unchecked") Map<String, Object> json = (Map<String, Object>) raw;
        String id = string(json.get("id"));
        if (id == null || !id.matches("[a-z][a-z0-9_-]{1,63}"))
            throw new IllegalArgumentException("invalid mod id");

        List<Entrypoint> entrypoints = new ArrayList<>();
        Object entrypointObject = json.get("entrypoints");
        if (entrypointObject instanceof Map<?, ?> entries) {
            addEntrypoints(entrypoints, entries.get("main"), "main");
            addEntrypoints(entrypoints, entries.get("server"), "server");
        }
        Map<String, String> dependencies = dependencyMap(json.get("depends"), "depends");
        Map<String, String> recommends = dependencyMap(json.get("recommends"), "recommends");
        Map<String, String> suggests = dependencyMap(json.get("suggests"), "suggests");
        Map<String, String> breaks = dependencyMap(json.get("breaks"), "breaks");
        Map<String, String> conflicts = dependencyMap(json.get("conflicts"), "conflicts");
        List<String> provides = stringList(json.get("provides"), "provides");
        List<String> mixinConfigs = mixinConfigs(json.get("mixins"));
        String environment = string(json.get("environment"));
        if (environment == null || environment.isBlank()) environment = "*";
        return new Candidate(id, string(json.get("name")), string(json.get("version")),
            environment, path, dependencies, recommends, suggests, breaks, conflicts,
            provides, entrypoints, mixinConfigs);
    }

    static List<Candidate> dependencyOrder(
        List<Candidate> candidates, BiPredicate<String, String> versionMatcher) {
        Map<String, Candidate> byId = new LinkedHashMap<>();
        for (Candidate candidate : candidates) {
            if (byId.put(candidate.id(), candidate) != null)
                throw new IllegalArgumentException("duplicate mod id: " + candidate.id());
        }
        for (Candidate candidate : candidates) {
            for (String provided : candidate.provides()) {
                Candidate previous = byId.putIfAbsent(provided, candidate);
                if (previous != null && previous != candidate)
                    throw new IllegalArgumentException("duplicate provided mod id: " + provided);
            }
        }
        validateDependencies(candidates, byId, versionMatcher);

        List<Candidate> order = new ArrayList<>();
        Set<String> visiting = new HashSet<>();
        Set<String> visited = new HashSet<>();
        for (Candidate candidate : candidates)
            visit(candidate, byId, visiting, visited, order);
        return List.copyOf(order);
    }

    private static void visit(Candidate candidate, Map<String, Candidate> byId,
                              Set<String> visiting, Set<String> visited,
                              List<Candidate> order) {
        if (visited.contains(candidate.id())) return;
        if (!visiting.add(candidate.id()))
            throw new IllegalArgumentException("cyclic mod dependency at " + candidate.id());
        for (String dependency : candidate.dependencies().keySet()) {
            if (BUILTIN_DEPENDENCIES.contains(dependency)) continue;
            Candidate required = byId.get(dependency);
            if (required == null)
                throw new IllegalArgumentException(
                    candidate.id() + " requires missing mod " + dependency);
            visit(required, byId, visiting, visited, order);
        }
        visiting.remove(candidate.id());
        visited.add(candidate.id());
        order.add(candidate);
    }

    private static void validateDependencies(
        List<Candidate> candidates, Map<String, Candidate> byId,
        BiPredicate<String, String> versionMatcher) {
        for (Candidate candidate : candidates) {
            for (Map.Entry<String, String> dependency : candidate.dependencies().entrySet()) {
                String actual = versionOfDependency(dependency.getKey(), byId);
                if (actual == null)
                    throw new IllegalArgumentException(candidate.id()
                        + " requires missing mod " + dependency.getKey());
                if (!versionMatcher.test(actual, dependency.getValue()))
                    throw new IllegalArgumentException(candidate.id() + " requires "
                        + dependency.getKey() + " " + dependency.getValue()
                        + " but found " + actual);
            }
            validateConflicts(candidate, candidate.breaks(), byId,
                "breaks", versionMatcher);
            validateConflicts(candidate, candidate.conflicts(), byId,
                "conflicts", versionMatcher);
        }
    }

    private static void validateConflicts(
        Candidate candidate, Map<String, String> conflicts,
        Map<String, Candidate> byId, String kind,
        BiPredicate<String, String> versionMatcher) {
        for (Map.Entry<String, String> conflict : conflicts.entrySet()) {
            String actual = versionOfDependency(conflict.getKey(), byId);
            if (actual != null && versionMatcher.test(actual, conflict.getValue()))
                throw new IllegalArgumentException(candidate.id() + " " + kind + " "
                    + conflict.getKey() + " " + conflict.getValue()
                    + " (found " + actual + ")");
        }
    }

    private static String versionOfDependency(String id, Map<String, Candidate> byId) {
        if (BUILTIN_DEPENDENCIES.contains(id)) return BUILTIN_VERSIONS.get(id);
        Candidate candidate = byId.get(id);
        return candidate == null || candidate.version() == null
            || candidate.version().isBlank() ? null : candidate.version();
    }

    private static Map<String, String> dependencyMap(Object value, String key) {
        if (value == null) return Map.of();
        if (!(value instanceof Map<?, ?> raw))
            throw new IllegalArgumentException(key + " must be an object");
        LinkedHashMap<String, String> result = new LinkedHashMap<>();
        for (Map.Entry<?, ?> entry : raw.entrySet()) {
            if (!(entry.getKey() instanceof String id) || id.isBlank())
                throw new IllegalArgumentException(key + " contains an invalid mod id");
            Object rawRequirement = entry.getValue();
            String requirement;
            if (rawRequirement instanceof String string && !string.isBlank()) {
                requirement = string;
            } else if (rawRequirement instanceof List<?> alternatives) {
                ArrayList<String> values = new ArrayList<>();
                for (Object alternative : alternatives) {
                    if (!(alternative instanceof String string) || string.isBlank())
                        throw new IllegalArgumentException(
                            key + " has an invalid requirement for " + id);
                    values.add(string);
                }
                if (values.isEmpty())
                    throw new IllegalArgumentException(
                        key + " has an invalid requirement for " + id);
                requirement = String.join(" || ", values);
            } else {
                throw new IllegalArgumentException(
                    key + " has an invalid requirement for " + id);
            }
            result.put(id, requirement);
        }
        return Map.copyOf(result);
    }

    private static List<String> stringList(Object value, String key) {
        if (value == null) return List.of();
        if (!(value instanceof List<?> list))
            throw new IllegalArgumentException(key + " must be an array");
        ArrayList<String> result = new ArrayList<>();
        for (Object item : list) {
            if (!(item instanceof String string) || string.isBlank())
                throw new IllegalArgumentException(key + " contains a non-string value");
            result.add(string);
        }
        return List.copyOf(result);
    }

    private static List<String> mixinConfigs(Object value) {
        List<String> configs = new ArrayList<>();
        if (value instanceof String config) configs.add(config);
        else if (value instanceof List<?> list) {
            for (Object config : list) {
                if (config instanceof String path) configs.add(path);
                else if (config instanceof Map<?, ?> object &&
                         object.get("config") instanceof String path)
                    configs.add(path);
            }
        }
        return List.copyOf(configs);
    }

    private static void addEntrypoints(
        List<Entrypoint> output, Object value, String key) {
        if (value instanceof String string) output.add(new Entrypoint(key, string));
        else if (value instanceof List<?> list) {
            for (Object entry : list) {
                if (entry instanceof String string)
                    output.add(new Entrypoint(key, string));
                else if (entry instanceof Map<?, ?> map &&
                         map.get("value") instanceof String string)
                    output.add(new Entrypoint(key, string));
            }
        } else if (value instanceof Map<?, ?> map &&
                   map.get("value") instanceof String string) {
            output.add(new Entrypoint(key, string));
        }
    }

    private static String javaMajorVersion() {
        String value = System.getProperty("java.specification.version", "0");
        if (value.startsWith("1.")) value = value.substring(2);
        int dot = value.indexOf('.');
        return dot < 0 ? value : value.substring(0, dot);
    }

    private static String string(Object value) {
        return value instanceof String ? (String) value : null;
    }

    record Candidate(String id, String name, String version, String environment,
                     Path path, Map<String, String> dependencies,
                     Map<String, String> recommends, Map<String, String> suggests,
                     Map<String, String> breaks, Map<String, String> conflicts,
                     List<String> provides, List<Entrypoint> entrypoints,
                     List<String> mixinConfigs) {
        boolean environmentMatchesServer() {
            return environment == null || environment.equals("*")
                || environment.equalsIgnoreCase("server")
                || environment.equalsIgnoreCase("universal");
        }
    }

    record Entrypoint(String key, String definition) { }
}
