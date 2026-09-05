package cppfm.bridge;

import java.io.File;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.jar.JarFile;

/**
 * Single entry point between the invocation JVM and the game class loader.
 *
 * <p>The native side loads this class with the application loader.  The
 * Minecraft shadow API, compatibility bootstrap, mixin classes, and mod jars
 * are then loaded by one child-first loader so class identity is not split
 * between the application and mod worlds.  The native bridge is installed on
 * the actual NativeBridge class object owned by that loader.</p>
 *
 * <p>The class intentionally has no compile-time dependency on the concrete
 * transformer implementation.  This keeps the bootstrap ABI stable while
 * allowing a versioned Knot-compatible loader to be selected at runtime.</p>
 */
public final class KnotLauncher {
    private static final Object LOCK = new Object();
    private static final String OFFICIAL_RUNTIME_PROPERTY = "cppfm.fabric-runtime";
    private static final String OFFICIAL_RUNTIME_ENV = "CPPFM_FABRIC_RUNTIME";
    private static final String OFFICIAL_PROVIDER_JAR_PROPERTY = "cppfm.fabric-provider-jar";
    private static final String OFFICIAL_PROVIDER_JAR_ENV = "CPPFM_FABRIC_PROVIDER_JAR";
    private static final String OFFICIAL_GAME_DIR_PROPERTY = "cppfm.fabric-game-dir";
    private static final String OFFICIAL_GAME_DIR_ENV = "CPPFM_FABRIC_GAME_DIR";
    private static final String OFFICIAL_PROVIDER_LAUNCHER =
        "cppfm.vendor.fabric.CppfmKnotLauncher";
    private static final String OFFICIAL_ENTRYPOINT =
        "cppfm.vendor.fabric.ShadowMain";

    private static ClassLoader loader;
    private static ClassLoader officialHost;
    private static Path officialTempRoot;
    private static Path officialEmptyMods;
    private static Path officialConfigDir;
    private static boolean officialMode;
    private static boolean officialHandoff;
    private static boolean officialEventsStarted;
    private static final Map<String, String> savedOfficialProperties = new LinkedHashMap<>();
    private static Class<?> runtimeType;
    private static Method runtimeBootstrap;
    private static Method runtimeShutdown;
    private static Method runtimeTick;
    private static Method runtimeJoin;
    private static Method runtimeQuit;
    private static Method runtimeChat;
    private static Method runtimeBlockBreak;
    private static Method runtimeBlockPlace;
    private static Method runtimeBlockClicked;
    private static Method runtimeCommand;
    private static Method runtimeDamage;
    private static Method runtimeSpawn;
    private static boolean started;

    private KnotLauncher() {}

    /** Implemented by the native invocation layer and registered once. */
    private static native boolean installBridge(Class<?> bridgeClass);

    public static boolean bootstrap(String classesDir, String modsDir, String configDir) {
        synchronized (LOCK) {
            if (started) return true;
            try {
                String officialRuntime = configured(OFFICIAL_RUNTIME_PROPERTY, OFFICIAL_RUNTIME_ENV);
                if (officialRuntime != null) {
                    bootstrapOfficial(classesDir, modsDir, configDir,
                                      Paths.get(officialRuntime).toAbsolutePath().normalize());
                } else {
                    bootstrapFallback(classesDir, modsDir, configDir);
                    logTransformerDiagnostics();
                }
                started = true;
                return true;
            } catch (Throwable failure) {
                closeLoader();
                closeOfficialResources();
                clear();
                Throwable cause = failure instanceof InvocationTargetException ite && ite.getCause() != null
                    ? ite.getCause() : failure;
                NativeBridge.logFallback("ERROR", "Knot bootstrap failed:\n" + describeFailure(cause));
                return false;
            }
        }
    }

    /**
     * Keep the historical dependency-free path intact.  The official path is
     * selected only by an explicit runtime directory property/environment
     * variable and never changes this loader's default behavior.
     */
    private static void bootstrapFallback(String classesDir, String modsDir,
                                          String configDir) throws Exception {
        List<URL> urls = urls(classesDir, modsDir);
        loader = makeLoader(urls.toArray(URL[]::new));
        prepareMixinConfigs(loader, modsDir);
        Class<?> bridge = Class.forName("cppfm.bridge.NativeBridge", true, loader);
        if (!installBridge(bridge))
            throw new IllegalStateException("native bridge registration failed");
        runtimeType = Class.forName("cppfm.bridge.CppModRuntime", true, loader);
        cacheRuntimeMethods(runtimeType);
        Boolean ok = (Boolean) invoke(runtimeBootstrap, modsDir, configDir);
        if (!Boolean.TRUE.equals(ok)) throw new IllegalStateException("mod bootstrap returned false");
    }

    /**
     * Start the pinned official Loader/Knot stack in this HotSpot process.
     * This method is intentionally opt-in: a normal cppfm launch continues to
     * use the dependency-free compatibility loader above.
     */
    private static void bootstrapOfficial(String classesDir, String modsDir,
                                          String configDir, Path runtimeRoot) throws Exception {
        if (!Files.isDirectory(runtimeRoot))
            throw new IOException("official Fabric runtime directory does not exist: " + runtimeRoot);

        Path providerJar = configuredPath(OFFICIAL_PROVIDER_JAR_PROPERTY, OFFICIAL_PROVIDER_JAR_ENV);
        if (providerJar == null) providerJar = runtimeRoot.resolve("cppfm-provider.jar");
        providerJar = providerJar.toAbsolutePath().normalize();
        requireFile(providerJar, "official provider jar");

        List<Path> runtimeJars = List.of(
            runtimeRoot.resolve("net/fabricmc/fabric-loader/0.16.9/fabric-loader-0.16.9.jar"),
            runtimeRoot.resolve("net/fabricmc/sponge-mixin/0.15.4+mixin.0.8.7/sponge-mixin-0.15.4+mixin.0.8.7.jar"),
            runtimeRoot.resolve("org/ow2/asm/asm/9.7.1/asm-9.7.1.jar"),
            runtimeRoot.resolve("org/ow2/asm/asm-analysis/9.7.1/asm-analysis-9.7.1.jar"),
            runtimeRoot.resolve("org/ow2/asm/asm-commons/9.7.1/asm-commons-9.7.1.jar"),
            runtimeRoot.resolve("org/ow2/asm/asm-tree/9.7.1/asm-tree-9.7.1.jar"),
            runtimeRoot.resolve("org/ow2/asm/asm-util/9.7.1/asm-util-9.7.1.jar"),
            runtimeRoot.resolve("net/fabricmc/intermediary/1.21.4/intermediary-1.21.4.jar"));
        for (Path jar : runtimeJars) requireFile(jar, "official runtime artifact");

        Path source = Paths.get(classesDir).toAbsolutePath().normalize();
        if (!Files.isDirectory(source)) throw new IOException("classes directory is missing: " + source);
        officialTempRoot = Files.createTempDirectory("cppfm-official-runtime-");
        Path shadowClasses = officialTempRoot.resolve("shadow-classes");
        Files.createDirectories(shadowClasses);
        copyOfficialShadow(source, shadowClasses);
        officialEmptyMods = officialTempRoot.resolve("empty-mods");
        Files.createDirectories(officialEmptyMods);
        officialConfigDir = Paths.get(configDir).toAbsolutePath().normalize();
        Files.createDirectories(officialConfigDir);

        Path gameDir = configuredPath(OFFICIAL_GAME_DIR_PROPERTY, OFFICIAL_GAME_DIR_ENV);
        if (gameDir == null) {
            Path modsPath = Paths.get(modsDir).toAbsolutePath().normalize();
            gameDir = modsPath.getParent();
        }
        if (gameDir == null) gameDir = Paths.get(".").toAbsolutePath().normalize();
        Files.createDirectories(gameDir);

        officialMode = true;
        saveOfficialProperty("fabric.development", "true");
        saveOfficialProperty("fabric.skipMcProvider", "true");
        saveOfficialProperty("cppfm.game-provider", "shadow");
        saveOfficialProperty("cppfm.shadow-classes", shadowClasses.toString());
        saveOfficialProperty("cppfm.game-dir", gameDir.toString());
        saveOfficialProperty("cppfm.provider.entrypoint", OFFICIAL_ENTRYPOINT);
        saveOfficialProperty("cppfm.official.embedded", "true");
        saveOfficialProperty("cppfm.official.empty-mods", officialEmptyMods.toString());
        saveOfficialProperty("cppfm.official.config-dir", officialConfigDir.toString());
        saveOfficialProperty("mixin.env.remapRefMap", "false");
        saveOfficialProperty("mixin.env.disableRefMap", "true");
        saveOfficialProperty("fabric.loader.useCompatibilityClassLoader", "false");

        List<URL> hostUrls = new ArrayList<>();
        hostUrls.add(providerJar.toUri().toURL());
        for (Path jar : runtimeJars) hostUrls.add(jar.toUri().toURL());
        officialHost = new cppfm.loader.KnotRuntimeClassLoader(
            hostUrls.toArray(URL[]::new), KnotLauncher.class.getClassLoader());

        // Knot.init() reads java.class.path to build its initial game code
        // source set.  Mirror the verified local runtime classpath while the
        // official bootstrap is running, then restore the C++ JVM value only
        // when the runtime is shut down.
        List<Path> knotClassPath = new ArrayList<>();
        knotClassPath.add(providerJar);
        knotClassPath.addAll(runtimeJars);
        saveOfficialProperty("java.class.path", joinPaths(knotClassPath));

        invokeMain(officialHost, OFFICIAL_PROVIDER_LAUNCHER,
                   new String[] {"--gameDir", gameDir.toString()});
        if (!officialHandoff || runtimeType == null)
            throw new IllegalStateException("official Knot returned without target NativeBridge handoff");
    }

    /** Called by the provider entrypoint after Knot has created its target loader. */
    public static void officialHandoff(ClassLoader target) throws Exception {
        synchronized (LOCK) {
            if (!officialMode) throw new IllegalStateException("official handoff outside official bootstrap");
            if (target == null) throw new IllegalArgumentException("official target classloader is null");
            if (officialHandoff) {
                if (loader != target) throw new IllegalStateException("official target classloader changed");
                return;
            }
            Class<?> bridge = Class.forName("cppfm.bridge.NativeBridge", true, target);
            if (!installBridge(bridge))
                throw new IllegalStateException("official target native bridge registration failed");
            loader = target;
            runtimeType = Class.forName("cppfm.bridge.CppModRuntime", true, target);
            cacheRuntimeMethods(runtimeType);
            officialHandoff = true;
            System.out.println("CPPFM_OFFICIAL_INPROCESS_HANDOFF targetClassLoader="
                               + target.getClass().getName()
                               + " bridgeClassLoader=" + bridge.getClassLoader().getClass().getName());
        }
    }

    /**
     * Start only the C++ event facade after official Fabric entrypoints have
     * initialized.  Passing the private empty mod root prevents a second mod
     * discovery/initialization pass; actual discovery belongs to Fabric
     * Loader, while CppModRuntime owns the native event bridge.
     */
    public static void officialFinalize() throws Exception {
        synchronized (LOCK) {
            if (!officialMode || !officialHandoff)
                throw new IllegalStateException("official event handoff is not ready");
            if (officialEventsStarted) return;
            Boolean ok = (Boolean) invoke(runtimeBootstrap,
                                          officialEmptyMods.toString(), officialConfigDir.toString());
            if (!Boolean.TRUE.equals(ok))
                throw new IllegalStateException("official event bridge bootstrap returned false");
            officialEventsStarted = true;
            System.out.println("CPPFM_OFFICIAL_EVENT_BRIDGE_READY classLoader="
                               + runtimeType.getClassLoader().getClass().getName());
        }
    }

    public static void shutdown() {
        synchronized (LOCK) {
            if (!started) {
                closeLoader();
                closeOfficialResources();
                clear();
                return;
            }
            try { invoke(runtimeShutdown); }
            catch (Throwable failure) { NativeBridge.logFallback("ERROR", "Knot shutdown failed: " + failure); }
            finally {
                closeLoader();
                closeOfficialResources();
                clear();
            }
        }
    }

    public static void onServerTick(long tick) { invokeVoid(runtimeTick, tick); }
    public static void onPlayerJoin(long handle) { invokeVoid(runtimeJoin, handle); }
    public static void onPlayerQuit(long handle) { invokeVoid(runtimeQuit, handle); }

    public static String onChat(long handle, String message) {
        if (!started) return message;
        Object result = invokeQuiet(runtimeChat, handle, message);
        return result instanceof String ? (String) result : null;
    }

    public static boolean onBlockBreak(long handle, int x, int y, int z, int state) {
        return booleanResult(invokeQuiet(runtimeBlockBreak, handle, x, y, z, state), true);
    }
    public static boolean onBlockPlace(long handle, int x, int y, int z, int state) {
        return booleanResult(invokeQuiet(runtimeBlockPlace, handle, x, y, z, state), true);
    }
    public static boolean onBlockClicked(long handle, int x, int y, int z, int state, int face) {
        return booleanResult(invokeQuiet(runtimeBlockClicked, handle, x, y, z, state, face), true);
    }
    public static String onCommand(long handle, String command) {
        if (!started) return command;
        Object result = invokeQuiet(runtimeCommand, handle, command);
        return result instanceof String ? (String) result : null;
    }
    public static boolean onEntityDamage(long playerHandle, long entityHandle,
                                         float amount, String cause) {
        return booleanResult(invokeQuiet(runtimeDamage, playerHandle, entityHandle, amount, cause), true);
    }
    public static boolean onMobSpawn(long entityHandle, double x, double y, double z) {
        return booleanResult(invokeQuiet(runtimeSpawn, entityHandle, x, y, z), true);
    }

    private static Object invokeQuiet(Method method, Object... args) {
        if (!started || method == null) return null;
        try { return invoke(method, args); }
        catch (Throwable failure) {
            NativeBridge.logFallback("ERROR", "Knot callback failed:\n" + describeFailure(failure));
            return null;
        }
    }

    private static void invokeVoid(Method method, Object... args) { invokeQuiet(method, args); }

    private static boolean booleanResult(Object value, boolean fallback) {
        return value instanceof Boolean ? (Boolean) value : fallback;
    }

    private static Object invoke(Method method, Object... args) throws ReflectiveOperationException {
        if (method == null) throw new IllegalStateException("Knot runtime method is not initialized");
        return method.invoke(null, args);
    }

    private static Method method(Class<?> type, String name, Class<?>... parameters)
            throws NoSuchMethodException {
        Method method = type.getMethod(name, parameters);
        method.setAccessible(true);
        return method;
    }

    private static void cacheRuntimeMethods(Class<?> type) throws NoSuchMethodException {
        runtimeBootstrap = method(type, "bootstrap", String.class, String.class);
        runtimeShutdown = method(type, "shutdown");
        runtimeTick = method(type, "onServerTick", long.class);
        runtimeJoin = method(type, "onPlayerJoin", long.class);
        runtimeQuit = method(type, "onPlayerQuit", long.class);
        runtimeChat = method(type, "onChat", long.class, String.class);
        runtimeBlockBreak = method(type, "onBlockBreak", long.class, int.class,
                                   int.class, int.class, int.class);
        runtimeBlockPlace = method(type, "onBlockPlace", long.class, int.class,
                                   int.class, int.class, int.class);
        runtimeBlockClicked = method(type, "onBlockClicked", long.class, int.class,
                                     int.class, int.class, int.class, int.class);
        runtimeCommand = method(type, "onCommand", long.class, String.class);
        runtimeDamage = method(type, "onEntityDamage", long.class, long.class,
                               float.class, String.class);
        runtimeSpawn = method(type, "onMobSpawn", long.class, double.class,
                              double.class, double.class);
    }

    private static void invokeMain(ClassLoader classLoader, String className,
                                   String[] args) throws Exception {
        Thread thread = Thread.currentThread();
        ClassLoader previous = thread.getContextClassLoader();
        try {
            thread.setContextClassLoader(classLoader);
            Class<?> type = Class.forName(className, true, classLoader);
            Method main = method(type, "main", String[].class);
            invoke(main, (Object) args);
        } finally {
            thread.setContextClassLoader(previous);
        }
    }

    private static String configured(String property, String environment) {
        String value = System.getProperty(property);
        if (value == null || value.isBlank()) value = System.getenv(environment);
        return value == null || value.isBlank() ? null : value.trim();
    }

    private static Path configuredPath(String property, String environment) {
        String value = configured(property, environment);
        return value == null ? null : Paths.get(value).toAbsolutePath().normalize();
    }

    private static void requireFile(Path path, String label) throws IOException {
        if (Files.isSymbolicLink(path) || !Files.isRegularFile(path))
            throw new IOException(label + " is not a regular file: " + path);
    }

    private static String joinPaths(List<Path> paths) {
        return String.join(File.pathSeparator, paths.stream().map(Path::toString).toList());
    }

    private static void saveOfficialProperty(String key, String value) {
        if (!savedOfficialProperties.containsKey(key))
            savedOfficialProperties.put(key, System.getProperty(key));
        if (value == null) System.clearProperty(key);
        else System.setProperty(key, value);
    }

    /**
     * Stage only the C++ shadow namespaces that are not owned by official
     * Loader/Mixin.  In particular, never let the application-loader stubs
     * define a second net.fabricmc or org.spongepowered type identity.
     */
    private static void copyOfficialShadow(Path source, Path destination) throws IOException {
        try (var stream = Files.walk(source)) {
            for (Path item : (Iterable<Path>) stream::iterator) {
                if (Files.isSymbolicLink(item)) continue;
                Path relative = source.relativize(item);
                if (excludedOfficialShadow(relative)) continue;
                Path target = destination.resolve(relative);
                if (Files.isDirectory(item)) {
                    Files.createDirectories(target);
                } else if (Files.isRegularFile(item)) {
                    Files.createDirectories(target.getParent());
                    Files.copy(item, target);
                }
            }
        }
    }

    private static boolean excludedOfficialShadow(Path relative) {
        String value = relative.toString().replace(File.separatorChar, '/');
        return value.startsWith("net/fabricmc/api/")
            || value.startsWith("net/fabricmc/loader/")
            || value.startsWith("org/spongepowered/")
            || value.startsWith("cppfm/loader/")
            // MixinDispatch is the small shared routing ledger used by the
            // event facade; the rest of cppfm.transform is the historical
            // dependency-free transformer and must not be loaded by official
            // Knot as a second transform chain.
            || (value.startsWith("cppfm/transform/")
                && !value.equals("cppfm/transform/MixinDispatch.class"))
            || value.equals("cppfm/bridge/KnotLauncher.class")
            || value.startsWith("cppfm/bridge/KnotLauncher$");
    }

    private static ClassLoader makeLoader(URL[] urls) throws ReflectiveOperationException {
        for (String name : List.of("cppfm.loader.KnotClassLoader",
                                  "cppfm.loader.KnotLikeClassLoader")) {
            try {
                Class<?> type = Class.forName(name, true, KnotLauncher.class.getClassLoader());
                Constructor<?> constructor = type.getConstructor(URL[].class, ClassLoader.class);
                return (ClassLoader) constructor.newInstance(urls, KnotLauncher.class.getClassLoader());
            } catch (ClassNotFoundException | NoSuchMethodException ignored) {
                // Try the next provider; the plain URL loader is the final
                // compatibility fallback for native-only deployments.
            }
        }
        return new URLClassLoader(urls, KnotLauncher.class.getClassLoader());
    }

    /**
     * Give the pre-definition transformer the same server mixin resources
     * that the metadata loader will later consume.  This is deliberately done
     * before CppModRuntime is resolved, because resolving an annotation-bearing
     * mixin can otherwise load its target class too early.
     */
    private static void prepareMixinConfigs(ClassLoader target, String modsDir) throws Exception {
        Method register;
        try { register = target.getClass().getMethod("registerMixinConfig", String.class); }
        catch (NoSuchMethodException ignored) { return; }
        Set<String> resources = new LinkedHashSet<>();
        Path directory = modsDir == null || modsDir.isBlank() ? null : Paths.get(modsDir);
        if (directory != null && Files.isDirectory(directory)) {
            try (var stream = Files.list(directory)) {
                List<Path> candidates = stream
                    .filter(item -> Files.isDirectory(item) || item.toString().endsWith(".jar"))
                    .sorted(Comparator.comparing(Path::toString)).toList();
                for (Path candidate : candidates) resources.addAll(mixinResources(candidate));
            }
        }
        for (String resource : resources) register.invoke(target, resource);
    }

    @SuppressWarnings("unchecked")
    private static List<String> mixinResources(Path candidate) throws Exception {
        String metadata;
        if (Files.isDirectory(candidate)) {
            Path file = candidate.resolve("fabric.mod.json");
            if (!Files.isRegularFile(file)) return List.of();
            metadata = Files.readString(file);
        } else {
            try (JarFile jar = new JarFile(candidate.toFile())) {
                var entry = jar.getJarEntry("fabric.mod.json");
                if (entry == null) return List.of();
                try (var stream = jar.getInputStream(entry)) {
                    metadata = new String(stream.readAllBytes(), java.nio.charset.StandardCharsets.UTF_8);
                }
            }
        }
        Object parsed = MiniJson.parse(metadata);
        if (!(parsed instanceof Map<?, ?> map)) return List.of();
        Object value = map.get("mixins");
        List<String> result = new ArrayList<>();
        if (value instanceof String name) result.add(name);
        else if (value instanceof List<?> list) {
            for (Object item : list) {
                if (item instanceof String name) result.add(name);
                else if (item instanceof Map<?, ?> object && object.get("config") instanceof String name)
                    result.add(name);
            }
        }
        return result;
    }

    private static List<URL> urls(String classesDir, String modsDir) throws IOException {
        List<URL> result = new ArrayList<>();
        if (classesDir != null && !classesDir.isBlank()) {
            Path path = Paths.get(classesDir);
            if (!Files.isDirectory(path)) throw new IOException("classes directory is missing: " + path);
            result.add(path.toUri().toURL());
        }
        if (modsDir != null && !modsDir.isBlank()) {
            Path path = Paths.get(modsDir);
            if (Files.isDirectory(path)) {
                try (var stream = Files.list(path)) {
                    List<Path> candidates = stream
                        .filter(item -> Files.isDirectory(item) || item.toString().endsWith(".jar"))
                        .sorted(Comparator.comparing(Path::toString)).toList();
                    for (Path item : candidates) result.add(item.toUri().toURL());
                } catch (UrlFailure failure) { throw failure.io; }
            }
        }
        return result;
    }

    private static void clear() {
        loader = null;
        officialHost = null;
        officialTempRoot = null;
        officialEmptyMods = null;
        officialConfigDir = null;
        officialMode = false;
        officialHandoff = false;
        officialEventsStarted = false;
        runtimeType = null;
        runtimeBootstrap = null;
        runtimeShutdown = null;
        runtimeTick = null;
        runtimeJoin = null;
        runtimeQuit = null;
        runtimeChat = null;
        runtimeBlockBreak = null;
        runtimeBlockPlace = null;
        runtimeBlockClicked = null;
        runtimeCommand = null;
        runtimeDamage = null;
        runtimeSpawn = null;
        started = false;
        savedOfficialProperties.clear();
    }

    /** Surface fail-closed transformer decisions in the native evidence log. */
    private static void logTransformerDiagnostics() {
        if (loader == null) return;
        try {
            Method diagnostics = loader.getClass().getMethod("getDiagnostics");
            Object value = diagnostics.invoke(loader);
            if (value instanceof Iterable<?> entries) {
                for (Object entry : entries)
                    NativeBridge.logFallback("WARN", "class transformer: " + entry);
            }
        } catch (Throwable failure) {
            NativeBridge.logFallback("WARN", "class transformer diagnostics unavailable: " + failure);
        }
    }

    private static String describeFailure(Throwable failure) {
        StringWriter output = new StringWriter();
        failure.printStackTrace(new PrintWriter(output));
        return output.toString();
    }

    private static void closeLoader() {
        if (!(loader instanceof URLClassLoader urls) || loader == officialHost) return;
        try { urls.close(); }
        catch (IOException failure) {
            NativeBridge.logFallback("WARN", "Knot classloader close failed: " + failure);
        }
    }

    private static void closeOfficialResources() {
        if (officialHost instanceof URLClassLoader urls) {
            try { urls.close(); }
            catch (IOException failure) {
                NativeBridge.logFallback("WARN", "official Fabric host close failed: " + failure);
            }
        }
        officialHost = null;
        for (Map.Entry<String, String> entry : savedOfficialProperties.entrySet()) {
            if (entry.getValue() == null) System.clearProperty(entry.getKey());
            else System.setProperty(entry.getKey(), entry.getValue());
        }
        savedOfficialProperties.clear();
        if (officialTempRoot != null) deleteTree(officialTempRoot);
    }

    private static void deleteTree(Path root) {
        if (!Files.exists(root)) return;
        try (var stream = Files.walk(root)) {
            stream.sorted(Comparator.reverseOrder()).forEach(path -> {
                try { Files.deleteIfExists(path); }
                catch (IOException failure) {
                    NativeBridge.logFallback("WARN", "official runtime cleanup failed: " + path);
                }
            });
        } catch (IOException failure) {
            NativeBridge.logFallback("WARN", "official runtime cleanup scan failed: " + failure);
        }
    }

    private static final class UrlFailure extends RuntimeException {
        private final IOException io;
        private UrlFailure(IOException failure) { super(failure); io = failure; }
    }
}
