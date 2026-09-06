package net.fabricmc.loader.api;

import java.io.File;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.function.BiConsumer;
import java.util.function.Consumer;

import net.fabricmc.api.EnvType;
import net.fabricmc.loader.api.entrypoint.EntrypointContainer;
import net.fabricmc.loader.api.metadata.ModEnvironment;

/**
 * Dependency-free implementation boundary for the Fabric Loader public API.
 *
 * <p>The real Loader owns this API when the opt-in official runtime is used.
 * The fallback runtime keeps the same binary method descriptors and publishes
 * its resolved metadata/entrypoints through the small runtime state below.</p>
 */
public interface FabricLoader {
    FabricLoader INSTANCE = new FabricLoader() { };

    static FabricLoader getInstance() { return INSTANCE; }

    default <T> List<T> getEntrypoints(String key, Class<T> type) {
        if (key == null || type == null) return List.of();
        ArrayList<T> result = new ArrayList<>();
        for (RuntimeState.Entry entry : RuntimeState.entrypoints.getOrDefault(key, List.of())) {
            if (type.isInstance(entry.value())) result.add(type.cast(entry.value()));
        }
        return List.copyOf(result);
    }

    default <T> List<EntrypointContainer<T>> getEntrypointContainers(String key, Class<T> type) {
        if (key == null || type == null) return List.of();
        ArrayList<EntrypointContainer<T>> result = new ArrayList<>();
        for (RuntimeState.Entry entry : RuntimeState.entrypoints.getOrDefault(key, List.of())) {
            if (type.isInstance(entry.value())) {
                T value = type.cast(entry.value());
                result.add(new BasicEntrypointContainer<>(value, entry.provider(), entry.definition()));
            }
        }
        return List.copyOf(result);
    }

    default <T> void invokeEntrypoints(String key, Class<T> type, Consumer<? super T> consumer) {
        if (consumer == null) throw new NullPointerException("consumer");
        for (T entrypoint : getEntrypoints(key, type)) consumer.accept(entrypoint);
    }

    default ObjectShare getObjectShare() { return RuntimeState.objectShare; }

    default boolean isModLoaded(String id) {
        if (id == null || id.isBlank()) return false;
        if ("minecraft".equals(id) || "fabricloader".equals(id) || "java".equals(id)) return true;
        if (RuntimeState.containers.containsKey(id)) return true;
        return splitProperty(System.getProperty("cppfm.loaded.mods", "")).contains(id);
    }

    default boolean isDevelopmentEnvironment() { return Boolean.getBoolean("fabric.development"); }
    default EnvType getEnvironmentType() { return EnvType.SERVER; }

    default Collection<ModContainer> getAllMods() {
        LinkedHashMap<String, ModContainer> resolved = new LinkedHashMap<>(RuntimeState.containers);
        Path gameDir = getGameDir();
        resolved.putIfAbsent("minecraft", BasicContainer.of("minecraft", "Minecraft", "1.21.4", gameDir,
            ModEnvironment.UNIVERSAL, List.of()));
        resolved.putIfAbsent("fabricloader", BasicContainer.of("fabricloader", "Fabric Loader", "0.16.9", gameDir,
            ModEnvironment.UNIVERSAL, List.of()));
        for (String id : splitProperty(System.getProperty("cppfm.loaded.mods", ""))) {
            if (id.equals("minecraft") || id.equals("fabricloader")) continue;
            String version = System.getProperty("cppfm.mod.version." + id, "");
            String name = System.getProperty("cppfm.mod.name." + id, id);
            Path root = pathProperty("cppfm.mod.root." + id, gameDir);
            resolved.putIfAbsent(id, BasicContainer.of(id, name, version, root,
                environment(System.getProperty("cppfm.mod.environment." + id, "*")),
                splitProperty(System.getProperty("cppfm.mod.provides." + id, ""))));
        }
        return List.copyOf(resolved.values());
    }

    default Optional<ModContainer> getModContainer(String id) {
        if (id == null) return Optional.empty();
        return getAllMods().stream()
            .filter(mod -> mod.getMetadata().getId().equals(id)
                || mod.getMetadata().getProvides().contains(id))
            .findFirst();
    }

    default MappingResolver getMappingResolver() { return MappingResolver.IDENTITY; }
    default Object getGameInstance() { return null; }

    default Path getGameDir() {
        String configured = System.getProperty("cppfm.game-dir");
        if (configured == null || configured.isBlank()) {
            String mods = System.getProperty("cppfm.mods.dir");
            if (mods != null && !mods.isBlank()) {
                Path parent = Paths.get(mods).toAbsolutePath().normalize().getParent();
                if (parent != null) configured = parent.toString();
            }
        }
        return pathProperty(configured, Paths.get(System.getProperty("user.dir", ".")));
    }

    default File getGameDirectory() { return getGameDir().toFile(); }
    default Path getConfigDir() {
        String configured = System.getProperty("cppfm.config-dir");
        if (configured == null || configured.isBlank()) configured = System.getProperty("cppfm.config.dir");
        return pathProperty(configured, getGameDir().resolve("config"));
    }
    default File getConfigDirectory() { return getConfigDir().toFile(); }

    /** Returns the launch arguments visible to a Fabric entrypoint. */
    default String[] getLaunchArguments(boolean sanitize) {
        String raw = System.getProperty("cppfm.launch.arguments", "");
        if (raw.isBlank()) return new String[0];
        return raw.trim().split("\\s+");
    }

    /** Used by the fallback runtime's metadata resolver and VersionPredicate. */
    static boolean matchesVersion(Version actual, String requirement) {
        return actual != null && matchesVersion(actual.toString(), requirement);
    }

    /** Parse the common Fabric Loader version predicates without external deps. */
    static boolean matchesVersion(String actual, String requirement) {
        if (actual == null || requirement == null || requirement.isBlank()) return false;
        for (String alternative : requirement.split("\\|\\|")) {
            if (matchesVersionConjunction(actual.trim(), alternative.trim())) return true;
        }
        return false;
    }

    static boolean matchesVersionConjunction(String actual, String requirement) {
        if (requirement.isEmpty() || requirement.equals("*")) return true;
        if ((requirement.startsWith("[") || requirement.startsWith("("))
            && (requirement.endsWith("]") || requirement.endsWith(")"))) {
            String inside = requirement.substring(1, requirement.length() - 1);
            String[] bounds = inside.split(",", -1);
            if (bounds.length == 2) {
                if (!bounds[0].isBlank()) {
                    int comparison = compareVersions(actual, bounds[0].trim());
                    if (comparison < 0 || (comparison == 0 && requirement.charAt(0) == '(')) return false;
                }
                if (!bounds[1].isBlank()) {
                    int comparison = compareVersions(actual, bounds[1].trim());
                    if (comparison > 0 || (comparison == 0 && requirement.charAt(requirement.length() - 1) == ')')) return false;
                }
                return true;
            }
        }
        String normalized = requirement.replace(',', ' ').trim();
        String[] terms = normalized.split("\\s+");
        for (String term : terms) {
            if (term.isEmpty()) continue;
            if (!matchesVersionTerm(actual, term)) return false;
        }
        return true;
    }

    static boolean matchesVersionTerm(String actual, String term) {
        String operator = "";
        for (String candidate : List.of(">=", "<=", ">", "<", "=", "^", "~")) {
            if (term.startsWith(candidate)) {
                operator = candidate;
                term = term.substring(candidate.length());
                break;
            }
        }
        if (term.isEmpty()) return false;
        String wildcard = term.toLowerCase();
        if (wildcard.equals("*") || wildcard.equals("x")) return true;
        if (wildcard.contains("x") || wildcard.contains("*")) {
            String[] expected = wildcard.replace('*', 'x').split("\\.");
            String[] observed = actual.split("[-+.]", -1);
            for (int i = 0; i < expected.length; ++i) {
                if (expected[i].equals("x")) break;
                if (i >= observed.length || !expected[i].equals(observed[i])) return false;
            }
            return true;
        }
        int comparison = compareVersions(actual, term);
        return switch (operator) {
            case ">=" -> comparison >= 0;
            case "<=" -> comparison <= 0;
            case ">" -> comparison > 0;
            case "<" -> comparison < 0;
            case "^" -> comparison >= 0 && compareVersions(actual, caretUpperBound(term)) < 0;
            case "~" -> comparison >= 0 && compareVersions(actual, tildeUpperBound(term)) < 0;
            default -> comparison == 0;
        };
    }

    static String caretUpperBound(String value) {
        int[] components = numericComponents(value);
        if (components[0] > 0) return (components[0] + 1) + ".0.0";
        if (components[1] > 0) return "0." + (components[1] + 1) + ".0";
        return "0.0." + (components[2] + 1);
    }

    static String tildeUpperBound(String value) {
        int[] components = numericComponents(value);
        return components[0] + "." + (components[1] + 1) + ".0";
    }

    static int compareVersions(String left, String right) {
        int[] a = numericComponents(left);
        int[] b = numericComponents(right);
        for (int i = 0; i < Math.max(a.length, b.length); ++i) {
            int av = i < a.length ? a[i] : 0;
            int bv = i < b.length ? b[i] : 0;
            if (av != bv) return Integer.compare(av, bv);
        }
        String ap = prerelease(left);
        String bp = prerelease(right);
        if (ap.isEmpty() && !bp.isEmpty()) return 1;
        if (!ap.isEmpty() && bp.isEmpty()) return -1;
        return ap.compareTo(bp);
    }

    static int[] numericComponents(String value) {
        String core = value == null ? "" : value.trim();
        int hyphen = core.indexOf('-');
        int plus = core.indexOf('+');
        int end = core.length();
        if (hyphen >= 0) end = Math.min(end, hyphen);
        if (plus >= 0) end = Math.min(end, plus);
        core = core.substring(0, end);
        String[] pieces = core.split("\\.", -1);
        int[] result = new int[Math.max(3, pieces.length)];
        for (int i = 0; i < pieces.length; ++i) {
            String digits = pieces[i].replaceFirst("^\\D+", "");
            if (digits.isEmpty()) result[i] = 0;
            else {
                try { result[i] = Integer.parseInt(digits); }
                catch (NumberFormatException ignored) { result[i] = 0; }
            }
        }
        return result;
    }

    static String prerelease(String value) {
        if (value == null) return "";
        int start = value.indexOf('-');
        if (start < 0) return "";
        int end = value.indexOf('+', start);
        return value.substring(start + 1, end < 0 ? value.length() : end);
    }

    static List<String> splitProperty(String value) {
        if (value == null || value.isBlank()) return List.of();
        ArrayList<String> result = new ArrayList<>();
        for (String item : value.split(",")) if (!item.isBlank()) result.add(item.trim());
        return List.copyOf(result);
    }

    static Path pathProperty(String value, Path fallback) {
        if (value == null || value.isBlank()) return fallback.toAbsolutePath().normalize();
        return Paths.get(value).toAbsolutePath().normalize();
    }

    static ModEnvironment environment(String value) {
        if (value == null || value.equals("*") || value.equalsIgnoreCase("universal"))
            return ModEnvironment.UNIVERSAL;
        if (value.equalsIgnoreCase("client")) return ModEnvironment.CLIENT;
        if (value.equalsIgnoreCase("server")) return ModEnvironment.SERVER;
        return ModEnvironment.UNIVERSAL;
    }

    /** Publish one resolved mod to the fallback Loader API. */
    static void registerMod(String id, String name, String version, Path root,
                            String environment, List<String> provides) {
        registerMod(id, name, version, root, environment, provides,
            Map.of(), Map.of(), Map.of(), Map.of(), Map.of());
    }

    /** Publish resolved dependency metadata along with a mod container. */
    static void registerMod(String id, String name, String version, Path root,
                            String environment, List<String> provides,
                            Map<String, String> depends, Map<String, String> recommends,
                            Map<String, String> suggests, Map<String, String> conflicts,
                            Map<String, String> breaks) {
        if (id == null || id.isBlank()) return;
        RuntimeState.containers.put(id, BasicContainer.of(id, name, version, root,
            environment(environment), provides, depends, recommends, suggests, conflicts, breaks));
    }

    /** Publish an initialized entrypoint and its provider container. */
    static void registerEntrypoint(String key, Object value, String modId, String definition) {
        if (key == null || value == null) return;
        ModContainer provider = getInstance().getModContainer(modId).orElse(null);
        RuntimeState.entrypoints.computeIfAbsent(key, ignored -> new CopyOnWriteArrayList<>())
            .add(new RuntimeState.Entry(value, provider, definition == null ? "" : definition));
    }

    /** Clear runtime-owned metadata between two embedded server lifetimes. */
    static void clearRuntime() {
        RuntimeState.containers.clear();
        RuntimeState.entrypoints.clear();
        RuntimeState.objectShare.clear();
    }

    /** Small immutable API objects used by the dependency-free loader path. */
    final class BasicContainer implements ModContainer {
        private final net.fabricmc.loader.api.metadata.ModMetadata metadata;
        private final Path root;

        private BasicContainer(String id, String name, String version, Path root,
                               ModEnvironment environment, List<String> provides,
                               Map<String, String> depends, Map<String, String> recommends,
                               Map<String, String> suggests, Map<String, String> conflicts,
                               Map<String, String> breaks) {
            this.metadata = new BasicMetadata(id, name, version, environment, provides,
                depends, recommends, suggests, conflicts, breaks);
            this.root = root == null ? null : root.toAbsolutePath().normalize();
        }

        static BasicContainer of(String id, String name, String version, Path root,
                                 ModEnvironment environment, List<String> provides) {
            return of(id, name, version, root, environment, provides,
                Map.of(), Map.of(), Map.of(), Map.of(), Map.of());
        }

        static BasicContainer of(String id, String name, String version, Path root,
                                 ModEnvironment environment, List<String> provides,
                                 Map<String, String> depends, Map<String, String> recommends,
                                 Map<String, String> suggests, Map<String, String> conflicts,
                                 Map<String, String> breaks) {
            return new BasicContainer(id, name, version, root, environment, provides,
                depends, recommends, suggests, conflicts, breaks);
        }

        @Override public net.fabricmc.loader.api.metadata.ModMetadata getMetadata() { return metadata; }
        @Override public List<Path> getRootPaths() { return root == null ? List.of() : List.of(root); }
        @Override public Path getRootPath() { return root; }
        @Override public Path getPath(String path) {
            if (root == null || path == null || path.isBlank()) return null;
            return root.resolve(path).normalize();
        }
    }

    final class BasicMetadata implements net.fabricmc.loader.api.metadata.ModMetadata {
        private final String id;
        private final String name;
        private final SemanticVersion version;
        private final ModEnvironment environment;
        private final List<String> provides;
        private final Collection<net.fabricmc.loader.api.metadata.ModDependency> depends;
        private final Collection<net.fabricmc.loader.api.metadata.ModDependency> recommends;
        private final Collection<net.fabricmc.loader.api.metadata.ModDependency> suggests;
        private final Collection<net.fabricmc.loader.api.metadata.ModDependency> conflicts;
        private final Collection<net.fabricmc.loader.api.metadata.ModDependency> breaks;

        BasicMetadata(String id, String name, String version, ModEnvironment environment,
                      List<String> provides, Map<String, String> depends,
                      Map<String, String> recommends, Map<String, String> suggests,
                      Map<String, String> conflicts, Map<String, String> breaks) {
            this.id = id == null ? "" : id;
            this.name = name == null || name.isBlank() ? this.id : name;
            this.version = BasicVersion.parse(version == null || version.isBlank() ? "0.0.0" : version);
            this.environment = environment == null ? ModEnvironment.UNIVERSAL : environment;
            this.provides = List.copyOf(provides == null ? List.of() : provides);
            this.depends = BasicDependency.of(net.fabricmc.loader.api.metadata.ModDependency.Kind.DEPENDS, depends);
            this.recommends = BasicDependency.of(net.fabricmc.loader.api.metadata.ModDependency.Kind.RECOMMENDS, recommends);
            this.suggests = BasicDependency.of(net.fabricmc.loader.api.metadata.ModDependency.Kind.SUGGESTS, suggests);
            this.conflicts = BasicDependency.of(net.fabricmc.loader.api.metadata.ModDependency.Kind.CONFLICTS, conflicts);
            this.breaks = BasicDependency.of(net.fabricmc.loader.api.metadata.ModDependency.Kind.BREAKS, breaks);
        }

        @Override public String getType() { return "regular"; }
        @Override public String getId() { return id; }
        @Override public String getName() { return name; }
        @Override public String getDescription() { return ""; }
        @Override public SemanticVersion getVersion() { return version; }
        @Override public ModEnvironment getEnvironment() { return environment; }
        @Override public Collection<String> getProvides() { return provides; }
        @Override public Collection<net.fabricmc.loader.api.metadata.ModDependency> getDependencies() {
            ArrayList<net.fabricmc.loader.api.metadata.ModDependency> result = new ArrayList<>();
            result.addAll(depends);
            result.addAll(recommends);
            result.addAll(suggests);
            result.addAll(conflicts);
            result.addAll(breaks);
            return List.copyOf(result);
        }
        @Override public Collection<net.fabricmc.loader.api.metadata.ModDependency> getDepends() { return depends; }
        @Override public Collection<net.fabricmc.loader.api.metadata.ModDependency> getRecommends() { return recommends; }
        @Override public Collection<net.fabricmc.loader.api.metadata.ModDependency> getSuggests() { return suggests; }
        @Override public Collection<net.fabricmc.loader.api.metadata.ModDependency> getConflicts() { return conflicts; }
        @Override public Collection<net.fabricmc.loader.api.metadata.ModDependency> getBreaks() { return breaks; }
        @Override public Collection<net.fabricmc.loader.api.metadata.Person> getAuthors() { return List.of(); }
        @Override public Collection<net.fabricmc.loader.api.metadata.Person> getContributors() { return List.of(); }
        @Override public net.fabricmc.loader.api.metadata.ContactInformation getContact() {
            return net.fabricmc.loader.api.metadata.ContactInformation.EMPTY;
        }
        @Override public Collection<String> getLicense() { return List.of(); }
        @Override public Optional<String> getIconPath(int size) { return Optional.empty(); }
        @Override public boolean containsCustomValue(String key) { return false; }
        @Override public net.fabricmc.loader.api.metadata.CustomValue getCustomValue(String key) { return null; }
        @Override public Map<String, net.fabricmc.loader.api.metadata.CustomValue> getCustomValues() { return Map.of(); }
        @Override public boolean containsCustomElement(String key) { return false; }
    }

    final class BasicDependency implements net.fabricmc.loader.api.metadata.ModDependency {
        private final Kind kind;
        private final String id;
        private final String requirement;

        private BasicDependency(Kind kind, String id, String requirement) {
            this.kind = kind;
            this.id = id;
            this.requirement = requirement == null || requirement.isBlank() ? "*" : requirement;
        }

        static Collection<net.fabricmc.loader.api.metadata.ModDependency> of(Kind kind,
                                                                              Map<String, String> values) {
            if (values == null || values.isEmpty()) return List.of();
            ArrayList<net.fabricmc.loader.api.metadata.ModDependency> result = new ArrayList<>();
            for (Map.Entry<String, String> entry : values.entrySet())
                result.add(new BasicDependency(kind, entry.getKey(), entry.getValue()));
            return List.copyOf(result);
        }

        @Override public Kind getKind() { return kind; }
        @Override public String getModId() { return id; }
        @Override public boolean matches(Version version) {
            return FabricLoader.matchesVersion(version, requirement);
        }
        @Override public Collection<net.fabricmc.loader.api.metadata.version.VersionPredicate>
        getVersionRequirements() {
            try { return List.of(net.fabricmc.loader.api.metadata.version.VersionPredicate.parse(requirement)); }
            catch (VersionParsingException failure) { return List.of(); }
        }
    }

    final class BasicVersion implements SemanticVersion {
        private final String value;
        private final int[] components;
        private final String prerelease;
        private final String build;

        private BasicVersion(String value) {
            this.value = value == null ? "" : value;
            String core = this.value;
            int plus = core.indexOf('+');
            this.build = plus < 0 ? "" : core.substring(plus + 1);
            if (plus >= 0) core = core.substring(0, plus);
            int hyphen = core.indexOf('-');
            this.prerelease = hyphen < 0 ? "" : core.substring(hyphen + 1);
            if (hyphen >= 0) core = core.substring(0, hyphen);
            String[] parts = core.split("\\.", -1);
            this.components = new int[Math.max(3, parts.length)];
            for (int i = 0; i < parts.length; ++i) {
                if (parts[i].equalsIgnoreCase("x") || parts[i].equals("*")) {
                    components[i] = COMPONENT_WILDCARD;
                    continue;
                }
                try { components[i] = Integer.parseInt(parts[i]); }
                catch (NumberFormatException ignored) { components[i] = 0; }
            }
        }

        static BasicVersion parse(String value) { return new BasicVersion(value); }
        @Override public int getVersionComponentCount() { return components.length; }
        @Override public int getVersionComponent(int component) {
            return component < 0 || component >= components.length ? COMPONENT_WILDCARD : components[component];
        }
        @Override public Optional<String> getPrereleaseKey() { return Optional.ofNullable(prerelease.isEmpty() ? null : prerelease); }
        @Override public Optional<String> getBuildKey() { return Optional.ofNullable(build.isEmpty() ? null : build); }
        @Override public boolean hasWildcard() {
            for (int component : components) if (component == COMPONENT_WILDCARD) return true;
            return false;
        }
        @Override public boolean isPreRelease() { return !prerelease.isEmpty(); }
        @Override public String toString() { return value; }
        @Override public int compareTo(Version other) {
            return FabricLoader.compareVersions(value, other == null ? "" : other.toString());
        }
    }

    final class BasicEntrypointContainer<T> implements EntrypointContainer<T> {
        private final T value;
        private final ModContainer provider;
        private final String definition;

        BasicEntrypointContainer(T value, ModContainer provider, String definition) {
            this.value = value;
            this.provider = provider;
            this.definition = definition;
        }

        @Override public T getEntrypoint() { return value; }
        @Override public ModContainer getProvider() { return provider; }
        @Override public String getDefinition() { return definition; }
    }

    final class BasicObjectShare implements ObjectShare {
        private final Map<String, Object> values = new HashMap<>();
        private final Map<String, List<BiConsumer<String, Object>>> waiters = new HashMap<>();

        @Override public synchronized Object get(String key) { return values.get(key); }

        @Override public synchronized void whenAvailable(String key, BiConsumer<String, Object> consumer) {
            if (consumer == null) throw new NullPointerException("consumer");
            if (values.containsKey(key)) {
                Object value = values.get(key);
                consumer.accept(key, value);
            } else {
                waiters.computeIfAbsent(key, ignored -> new ArrayList<>()).add(consumer);
            }
        }

        @Override public Object put(String key, Object value) {
            List<BiConsumer<String, Object>> callbacks;
            Object previous;
            synchronized (this) {
                previous = values.put(key, value);
                callbacks = waiters.remove(key);
            }
            if (callbacks != null) for (BiConsumer<String, Object> callback : callbacks)
                callback.accept(key, value);
            return previous;
        }

        @Override public Object putIfAbsent(String key, Object value) {
            List<BiConsumer<String, Object>> callbacks = null;
            synchronized (this) {
                if (values.containsKey(key)) return values.get(key);
                values.put(key, value);
                callbacks = waiters.remove(key);
            }
            if (callbacks != null) for (BiConsumer<String, Object> callback : callbacks)
                callback.accept(key, value);
            return null;
        }

        @Override public synchronized Object remove(String key) { return values.remove(key); }
        synchronized void clear() { values.clear(); waiters.clear(); }
    }

    final class RuntimeState {
        private static final Map<String, ModContainer> containers = new LinkedHashMap<>();
        private static final Map<String, List<Entry>> entrypoints = new LinkedHashMap<>();
        private static final BasicObjectShare objectShare = new BasicObjectShare();

        private RuntimeState() { }
        record Entry(Object value, ModContainer provider, String definition) { }
    }
}
