package cppfm.bridge;

import java.io.IOException;
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
    private static ClassLoader loader;
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
                List<URL> urls = urls(classesDir, modsDir);
                loader = makeLoader(urls.toArray(URL[]::new));
                prepareMixinConfigs(loader, modsDir);
                Class<?> bridge = Class.forName("cppfm.bridge.NativeBridge", true, loader);
                if (!installBridge(bridge))
                    throw new IllegalStateException("native bridge registration failed");
                runtimeType = Class.forName("cppfm.bridge.CppModRuntime", true, loader);
                runtimeBootstrap = method(runtimeType, "bootstrap", String.class, String.class);
                runtimeShutdown = method(runtimeType, "shutdown");
                runtimeTick = method(runtimeType, "onServerTick", long.class);
                runtimeJoin = method(runtimeType, "onPlayerJoin", long.class);
                runtimeQuit = method(runtimeType, "onPlayerQuit", long.class);
                runtimeChat = method(runtimeType, "onChat", long.class, String.class);
                runtimeBlockBreak = method(runtimeType, "onBlockBreak", long.class, int.class,
                                           int.class, int.class, int.class);
                runtimeBlockPlace = method(runtimeType, "onBlockPlace", long.class, int.class,
                                           int.class, int.class, int.class);
                runtimeBlockClicked = method(runtimeType, "onBlockClicked", long.class, int.class,
                                             int.class, int.class, int.class, int.class);
                runtimeCommand = method(runtimeType, "onCommand", long.class, String.class);
                runtimeDamage = method(runtimeType, "onEntityDamage", long.class, long.class,
                                       float.class, String.class);
                runtimeSpawn = method(runtimeType, "onMobSpawn", long.class, double.class,
                                      double.class, double.class);
                Boolean ok = (Boolean) invoke(runtimeBootstrap, modsDir, configDir);
                if (!Boolean.TRUE.equals(ok)) throw new IllegalStateException("mod bootstrap returned false");
                started = true;
                return true;
            } catch (Throwable failure) {
                closeLoader();
                clear();
                Throwable cause = failure instanceof InvocationTargetException ite && ite.getCause() != null
                    ? ite.getCause() : failure;
                NativeBridge.logFallback("ERROR", "Knot bootstrap failed: " + cause);
                return false;
            }
        }
    }

    public static void shutdown() {
        synchronized (LOCK) {
            if (!started) {
                clear();
                return;
            }
            try { invoke(runtimeShutdown); }
            catch (Throwable failure) { NativeBridge.logFallback("ERROR", "Knot shutdown failed: " + failure); }
            finally {
                closeLoader();
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
            NativeBridge.logFallback("ERROR", "Knot callback failed: " + failure);
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
    }

    private static void closeLoader() {
        if (!(loader instanceof URLClassLoader urls)) return;
        try { urls.close(); }
        catch (IOException failure) {
            NativeBridge.logFallback("WARN", "Knot classloader close failed: " + failure);
        }
    }

    private static final class UrlFailure extends RuntimeException {
        private final IOException io;
        private UrlFailure(IOException failure) { super(failure); io = failure; }
    }
}
