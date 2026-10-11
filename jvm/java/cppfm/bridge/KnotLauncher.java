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
import java.util.Collection;
import java.util.Comparator;
import java.util.LinkedHashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.jar.JarFile;
import java.util.function.Supplier;
import java.util.concurrent.CompletableFuture;

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
    private static final Object NOT_INVOKED = new Object();
    private static final String OFFICIAL_RUNTIME_PROPERTY = "cppfm.fabric-runtime";
    private static final String OFFICIAL_RUNTIME_ENV = "CPPFM_FABRIC_RUNTIME";
    private static final String OFFICIAL_PROVIDER_JAR_PROPERTY = "cppfm.fabric-provider-jar";
    private static final String OFFICIAL_PROVIDER_JAR_ENV = "CPPFM_FABRIC_PROVIDER_JAR";
    private static final String OFFICIAL_GAME_DIR_PROPERTY = "cppfm.fabric-game-dir";
    private static final String OFFICIAL_GAME_DIR_ENV = "CPPFM_FABRIC_GAME_DIR";
    private static final String OFFICIAL_FABRIC_API_PRESENT_PROPERTY =
        "cppfm.official.fabric-api-present";
    private static final String YARN_MAPPING_RELATIVE_PATH =
        "net/fabricmc/yarn/1.21.4+build.8/yarn-1.21.4+build.8-v2.jar";
    private static final String OFFICIAL_PROVIDER_LAUNCHER =
        "cppfm.vendor.fabric.CppfmKnotLauncher";
    private static final String OFFICIAL_ENTRYPOINT =
        "cppfm.vendor.fabric.ShadowMain";

    private static ClassLoader loader;
    private static ClassLoader officialHost;
    private static Path officialTempRoot;
    private static Path officialEmptyMods;
    private static Path officialConfigDir;
    private static NestedJarSupport.Expansion fallbackModExpansion;
    private static boolean officialMode;
    private static boolean officialHandoff;
    private static boolean officialEventsStarted;
    private static final Map<String, String> savedOfficialProperties = new LinkedHashMap<>();
    private static Class<?> runtimeType;
    private static Method runtimeBootstrap;
    private static Method runtimeShutdown;
    private static Method runtimeServerShutdown;
    private static Method runtimeServerStopping;
    private static Method runtimeServerWorldsLoaded;
    private static Method runtimeWorldsUnloading;
    private static Method runtimeServerStopped;
    private static Method runtimeTick;
    private static Method runtimeJoin;
    private static Method runtimeWorldChange;
    private static Method runtimeQuit;
    private static Method runtimePlayerConfiguration;
    private static Method runtimePlayerConfigurationAborted;
    private static Method runtimePluginMessage;
    private static Method runtimeChat;
    private static Method runtimeBlockBreak;
    private static Method runtimeBlockPlace;
    private static Method runtimeBlockClicked;
    private static Method runtimeCommand;
    private static Method runtimeDamage;
    private static Method runtimeAllowDeath;
    private static Method runtimeDamageApplied;
    private static Method runtimeSpawn;
    private static Method runtimeMobLoad;
    private static Method runtimeMobUnload;
    private static Method runtimeDataPackReload;
    private static volatile boolean started;
    private static boolean bootstrapping;
    private static boolean knotProviderSelected;
    private static boolean shutdownInProgress;
    private static boolean shutdownCleanupPending;
    private static boolean shutdownCallbackComplete;
    private static Thread shutdownFinalizerThread;
    private static int runtimeCalls;
    private static final ThreadLocal<Integer> runtimeLeaseDepth =
        ThreadLocal.withInitial(() -> 0);

    private KnotLauncher() {}

    private enum BootstrapAdmission { STARTED, ALREADY_STARTED, REJECTED }

    /** Implemented by the native invocation layer and registered once. */
    private static native boolean installBridge(Class<?> bridgeClass);

    public static boolean bootstrap(String classesDir, String modsDir, String configDir) {
        BootstrapAdmission admission = beginBootstrap();
        if (admission == BootstrapAdmission.ALREADY_STARTED) return true;
        if (admission == BootstrapAdmission.REJECTED) return false;
        Throwable failure = null;
        try {
            String officialRuntime = configured(OFFICIAL_RUNTIME_PROPERTY, OFFICIAL_RUNTIME_ENV);
            if (officialRuntime != null) {
                bootstrapOfficial(classesDir, modsDir, configDir,
                                  Paths.get(officialRuntime).toAbsolutePath().normalize());
            } else {
                bootstrapFallback(classesDir, modsDir, configDir);
                logTransformerDiagnostics();
            }
        } catch (Throwable caught) {
            failure = caught;
        }
        return finishBootstrap(failure);
    }

    private static BootstrapAdmission beginBootstrap() {
        synchronized (LOCK) {
            if (shutdownInProgress) return BootstrapAdmission.REJECTED;
            if (started) return BootstrapAdmission.ALREADY_STARTED;
            if (bootstrapping) return BootstrapAdmission.REJECTED;
            bootstrapping = true;
            knotProviderSelected = true;
            return BootstrapAdmission.STARTED;
        }
    }

    /** Publish a completed bootstrap atomically with a concurrent shutdown request. */
    private static boolean finishBootstrap(Throwable failure) {
        boolean shutdownAfterBootstrap = false;
        synchronized (LOCK) {
            bootstrapping = false;
            if (failure == null) {
                started = true;
                shutdownAfterBootstrap = shutdownCleanupPending || shutdownInProgress;
                if (shutdownAfterBootstrap) {
                    shutdownInProgress = true;
                    shutdownCleanupPending = true;
                }
                if (!shutdownAfterBootstrap) return true;
            } else {
                closeLoader();
                closeOfficialResources();
                clear();
            }
        }
        if (failure == null) {
            completeShutdownIfQuiescent();
            return false;
        }
        Throwable cause = failure instanceof InvocationTargetException ite && ite.getCause() != null
            ? ite.getCause() : failure;
        NativeBridge.logFallback("ERROR", "Knot bootstrap failed:\n" + describeFailure(cause));
        return false;
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
        prepareAccessWideners(loader, modsDir);
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

        Path yarnMappings = runtimeRoot.resolve(YARN_MAPPING_RELATIVE_PATH);
        requireFile(yarnMappings, "pinned Yarn 1.21.4 intermediary-to-named mappings");

        List<Path> runtimeJars = List.of(
            runtimeRoot.resolve("net/fabricmc/fabric-loader/0.16.9/fabric-loader-0.16.9.jar"),
            runtimeRoot.resolve("net/fabricmc/sponge-mixin/0.15.4+mixin.0.8.7/sponge-mixin-0.15.4+mixin.0.8.7.jar"),
            runtimeRoot.resolve("org/ow2/asm/asm/9.7.1/asm-9.7.1.jar"),
            runtimeRoot.resolve("org/ow2/asm/asm-analysis/9.7.1/asm-analysis-9.7.1.jar"),
            runtimeRoot.resolve("org/ow2/asm/asm-commons/9.7.1/asm-commons-9.7.1.jar"),
            runtimeRoot.resolve("org/ow2/asm/asm-tree/9.7.1/asm-tree-9.7.1.jar"),
            runtimeRoot.resolve("org/ow2/asm/asm-util/9.7.1/asm-util-9.7.1.jar"),
            yarnMappings,
            runtimeRoot.resolve("net/fabricmc/intermediary/1.21.4/intermediary-1.21.4.jar"));
        for (Path jar : runtimeJars) requireFile(jar, "official runtime artifact");
        List<Path> runtimeLibraries = libraryPaths();

        Path modsPath = Paths.get(modsDir).toAbsolutePath().normalize();
        Path gameDir = configuredPath(OFFICIAL_GAME_DIR_PROPERTY, OFFICIAL_GAME_DIR_ENV);
        if (gameDir == null) gameDir = modsPath.getParent();
        if (gameDir == null) gameDir = Paths.get(".").toAbsolutePath().normalize();
        Files.createDirectories(gameDir);
        Set<String> officialFabricClasses = fabricApiClasses(modsPath);
        boolean officialFabricApiPresent = !officialFabricClasses.isEmpty();

        Path source = Paths.get(classesDir).toAbsolutePath().normalize();
        if (!Files.isDirectory(source)) throw new IOException("classes directory is missing: " + source);
        officialTempRoot = Files.createTempDirectory("cppfm-official-runtime-");
        Path shadowClasses = officialTempRoot.resolve("shadow-classes");
        Files.createDirectories(shadowClasses);
        copyOfficialShadow(source, shadowClasses, officialFabricClasses);
        Path stagedMods = officialTempRoot.resolve("staged-mods");
        if (Files.isDirectory(modsPath)) {
            cppfm.loader.IntermediaryNamedMappings mappings =
                cppfm.loader.IntermediaryNamedMappings.load(yarnMappings);
            cppfm.loader.ModJarStager.Result staged =
                cppfm.loader.ModJarStager.stage(modsPath, stagedMods, mappings);
            System.out.println("CPPFM_MODS_STAGED mods=" + staged.modCount()
                               + " classes=" + staged.classCount()
                               + " resources=" + staged.resourceCount());
        } else Files.createDirectories(stagedMods);
        officialEmptyMods = officialTempRoot.resolve("empty-mods");
        Files.createDirectories(officialEmptyMods);
        officialConfigDir = Paths.get(configDir).toAbsolutePath().normalize();
        Files.createDirectories(officialConfigDir);

        officialMode = true;
        saveOfficialProperty("fabric.development", "true");
        // Fabric discovers only the staged copy. The source mods directory
        // remains read-only and is never remapped in place.
        saveOfficialProperty("fabric.modsFolder", stagedMods.toString());
        saveOfficialProperty("fabric.skipMcProvider", "true");
        saveOfficialProperty("cppfm.game-provider", "shadow");
        saveOfficialProperty("cppfm.shadow-classes", shadowClasses.toString());
        saveOfficialProperty("cppfm.game-dir", gameDir.toString());
        saveOfficialProperty(OFFICIAL_FABRIC_API_PRESENT_PROPERTY,
                             Boolean.toString(officialFabricApiPresent));
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
        for (Path library : runtimeLibraries) hostUrls.add(library.toUri().toURL());
        officialHost = new cppfm.loader.KnotRuntimeClassLoader(
            hostUrls.toArray(URL[]::new), KnotLauncher.class.getClassLoader());

        // Knot.init() reads java.class.path to build its initial game code
        // source set.  Mirror the verified local runtime classpath while the
        // official bootstrap is running, then restore the C++ JVM value only
        // when the runtime is shut down.
        List<Path> knotClassPath = new ArrayList<>();
        knotClassPath.add(providerJar);
        knotClassPath.addAll(runtimeJars);
        knotClassPath.addAll(runtimeLibraries);
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
        Method bootstrap;
        String modsDirectory;
        String configDirectory;
        synchronized (LOCK) {
            if (!officialMode || !officialHandoff)
                throw new IllegalStateException("official event handoff is not ready");
            if (officialEventsStarted) return;
            officialEventsStarted = true;
            bootstrap = runtimeBootstrap;
            modsDirectory = officialEmptyMods.toString();
            configDirectory = officialConfigDir.toString();
        }
        try {
            Boolean ok = (Boolean) invoke(bootstrap, modsDirectory, configDirectory);
            if (!Boolean.TRUE.equals(ok))
                throw new IllegalStateException("official event bridge bootstrap returned false");
            System.out.println("CPPFM_OFFICIAL_EVENT_BRIDGE_READY classLoader="
                               + runtimeType.getClassLoader().getClass().getName());
        } catch (Exception failure) {
            synchronized (LOCK) { officialEventsStarted = false; }
            throw failure;
        }
    }

    public static void onServerStopping() {
        invokeRuntimeLifecycle(() -> runtimeServerStopping, "SERVER_STOPPING");
    }

    public static void onServerWorldsLoaded() {
        invokeRuntimeLifecycle(() -> runtimeServerWorldsLoaded, "server worlds loaded");
    }

    public static void onWorldsUnloading() {
        invokeRuntimeLifecycle(() -> runtimeWorldsUnloading, "ServerWorldEvents.UNLOAD");
    }

    public static void onServerStopped() {
        invokeRuntimeLifecycle(() -> runtimeServerStopped, "SERVER_STOPPED");
    }

    public static void onServerShutdown() {
        onServerStopping();
        onWorldsUnloading();
        onServerStopped();
    }

    private static Method beginRuntimeCall(Supplier<Method> methodSupplier) {
        synchronized (LOCK) {
            Method method = methodSupplier.get();
            int depth = runtimeLeaseDepth.get();
            boolean finalizerReentry = shutdownFinalizerThread == Thread.currentThread();
            if (!started || method == null ||
                (shutdownInProgress && depth == 0 && !finalizerReentry))
                return null;
            runtimeCalls++;
            runtimeLeaseDepth.set(depth + 1);
            return method;
        }
    }

    private static void finishRuntimeCall() {
        boolean completeShutdown;
        synchronized (LOCK) {
            int depth = runtimeLeaseDepth.get();
            if (depth <= 1) runtimeLeaseDepth.remove();
            else runtimeLeaseDepth.set(depth - 1);
            if (runtimeCalls > 0) runtimeCalls--;
            completeShutdown = runtimeCalls == 0 && shutdownInProgress &&
                shutdownCleanupPending;
        }
        if (completeShutdown) completeShutdownIfQuiescent();
    }

    private static void invokeRuntimeLifecycle(Supplier<Method> method,
                                               String eventName) {
        Method target = beginRuntimeCall(method);
        if (target == null) return;
        try {
            invoke(target);
        } catch (Throwable failure) {
            NativeBridge.logFallback("ERROR", "Knot " + eventName +
                " callback failed:\n" + describeFailure(failure));
            throw new IllegalStateException("Knot " + eventName + " dispatch failed", failure);
        } finally {
            finishRuntimeCall();
        }
    }

    public static void shutdown() {
        synchronized (LOCK) {
            if (bootstrapping) {
                shutdownInProgress = true;
                shutdownCleanupPending = true;
                shutdownCallbackComplete = false;
                return;
            }
            if (shutdownInProgress) return;
            shutdownInProgress = true;
            shutdownCleanupPending = true;
            shutdownCallbackComplete = false;
        }
        completeShutdownIfQuiescent();
    }

    /** True once native dispatch has selected this class as the active provider. */
    public static boolean isKnotProviderSelected() {
        synchronized (LOCK) { return knotProviderSelected; }
    }

    /**
     * Run the mod shutdown callback only after every admitted callback has
     * returned.  The callback and loader teardown both run outside LOCK.
     */
    private static void completeShutdownIfQuiescent() {
        Method runtimeMethod;
        synchronized (LOCK) {
            if (!shutdownInProgress || !shutdownCleanupPending || runtimeCalls != 0)
                return;
            shutdownCleanupPending = false;
            runtimeMethod = started && !shutdownCallbackComplete
                ? runtimeShutdown : null;
            shutdownFinalizerThread = Thread.currentThread();
        }

        Throwable failure = null;
        try {
            if (runtimeMethod != null) invoke(runtimeMethod);
        } catch (Throwable caught) {
            failure = caught;
        } finally {
            synchronized (LOCK) {
                shutdownFinalizerThread = null;
                if (runtimeMethod != null) shutdownCallbackComplete = true;
                if (runtimeCalls == 0) finishShutdownLocked();
                else shutdownCleanupPending = true;
            }
        }
        if (failure != null)
            NativeBridge.logFallback("ERROR", "Knot shutdown callback failed:\n"
                + describeFailure(failure));
    }

    /** Must be called with LOCK held; no user callback runs in this method. */
    private static void finishShutdownLocked() {
        closeLoader();
        closeOfficialResources();
        clear();
        shutdownInProgress = false;
    }

    public static void onServerTick(long tick) { invokeVoid(() -> runtimeTick, tick); }
    public static void onPlayerJoin(long handle) { invokeVoid(() -> runtimeJoin, handle); }
    public static void onPlayerWorldChange(long handle, int sourceDimension,
                                           int destinationDimension) {
        invokeVoid(() -> runtimeWorldChange, handle, sourceDimension,
                   destinationDimension);
    }
    public static void onPlayerQuit(long handle) { invokeVoid(() -> runtimeQuit, handle); }
    public static void onPlayerConfiguration(long handle) {
        invokeVoid(() -> runtimePlayerConfiguration, handle);
    }
    public static void onPlayerConfigurationAborted(long handle) {
        invokeVoid(() -> runtimePlayerConfigurationAborted, handle);
    }
    public static void onPluginMessage(long handle, int phase, String channel,
                                       byte[] payload) {
        invokeVoid(() -> runtimePluginMessage, handle, phase, channel, payload);
    }

    public static String onChat(long handle, String message) {
        if (!started) return message;
        Object result = invokeQuiet(() -> runtimeChat, handle, message);
        if (result == NOT_INVOKED) return message;
        return result instanceof String ? (String) result : null;
    }

    public static boolean onBlockBreak(long handle, int x, int y, int z, int state) {
        return booleanResult(invokeQuiet(() -> runtimeBlockBreak,
            handle, x, y, z, state), true);
    }
    public static boolean onBlockPlace(long handle, int x, int y, int z, int state) {
        return booleanResult(invokeQuiet(() -> runtimeBlockPlace,
            handle, x, y, z, state), true);
    }
    public static boolean onBlockClicked(long handle, int x, int y, int z, int state, int face) {
        return booleanResult(invokeQuiet(() -> runtimeBlockClicked,
            handle, x, y, z, state, face), true);
    }
    public static String onCommand(long handle, String command) {
        if (!started) return command;
        Object result = invokeQuiet(() -> runtimeCommand, handle, command);
        if (result == NOT_INVOKED) return command;
        return result instanceof String ? (String) result : null;
    }
    public static boolean onEntityDamage(long playerHandle, long entityHandle,
                                         float amount, String cause) {
        return booleanResult(invokeQuiet(() -> runtimeDamage,
            playerHandle, entityHandle, amount, cause), true);
    }
    public static boolean onEntityAllowDeath(long playerHandle, long entityHandle,
                                             float amount, String cause) {
        return booleanResult(
            invokeQuiet(() -> runtimeAllowDeath,
                playerHandle, entityHandle, amount, cause), true);
    }
    public static void onEntityDamageApplied(long playerHandle, long entityHandle,
                                             float baseDamage, float damageTaken,
                                             boolean blocked, boolean died,
                                             String cause) {
        invokeVoid(() -> runtimeDamageApplied, playerHandle, entityHandle, baseDamage,
                   damageTaken, blocked, died, cause);
    }
    public static boolean onMobSpawn(long entityHandle, double x, double y,
                                     double z, int dimension) {
        return booleanResult(
            invokeQuiet(() -> runtimeSpawn,
                entityHandle, x, y, z, dimension), true);
    }
    public static void onMobLoad(long entityHandle, int dimension) {
        invokeVoid(() -> runtimeMobLoad, entityHandle, dimension);
    }
    public static void onMobUnload(long entityHandle, int dimension) {
        invokeVoid(() -> runtimeMobUnload, entityHandle, dimension);
    }

    public static CompletableFuture<Void> onDataPackReload() {
        return onDataPackReload(List.of());
    }

    public static CompletableFuture<Void> onDataPackReload(Collection<String> dataPacks) {
        Method target = beginRuntimeCall(() -> runtimeDataPackReload);
        if (target == null) return CompletableFuture.failedFuture(
            new IllegalStateException("Knot runtime is not available for resource reload"));
        Object returned;
        try {
            returned = invoke(target, dataPacks);
        } catch (Throwable failure) {
            NativeBridge.logFallback("ERROR", "Knot data-pack reload callback failed:\n" +
                describeFailure(failure));
            finishRuntimeCall();
            return CompletableFuture.failedFuture(failure);
        }
        if (returned instanceof CompletableFuture<?> stage) {
            CompletableFuture<Void> result = new CompletableFuture<>();
            stage.whenComplete((value, failure) -> {
                try {
                    if (failure != null) result.completeExceptionally(unwrapCompletion(failure));
                    else if (value instanceof Boolean success && !success)
                        result.completeExceptionally(new IllegalStateException(
                            "Knot resource reload callback reported failure"));
                    else result.complete(null);
                } finally {
                    finishRuntimeCall();
                }
            });
            return result;
        }
        try {
            if (Boolean.TRUE.equals(returned)) return CompletableFuture.completedFuture(null);
            return CompletableFuture.failedFuture(new IllegalStateException(
                "Knot resource reload callback returned an unsupported result"));
        } finally {
            finishRuntimeCall();
        }
    }

    private static Throwable unwrapCompletion(Throwable failure) {
        Throwable cause = failure;
        while (cause instanceof java.util.concurrent.CompletionException &&
                cause.getCause() != null) cause = cause.getCause();
        return cause;
    }

    private static Object invokeQuiet(Supplier<Method> method, Object... args) {
        Method target = beginRuntimeCall(method);
        if (target == null) return NOT_INVOKED;
        try { return invoke(target, args); }
        catch (Throwable failure) {
            NativeBridge.logFallback("ERROR", "Knot callback failed:\n" + describeFailure(failure));
            return null;
        } finally {
            finishRuntimeCall();
        }
    }

    private static void invokeVoid(Supplier<Method> method, Object... args) {
        invokeQuiet(method, args);
    }

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
        runtimeServerShutdown = method(type, "onServerShutdown");
        runtimeServerStopping = method(type, "onServerStopping");
        runtimeServerWorldsLoaded = method(type, "onServerWorldsLoaded");
        runtimeWorldsUnloading = method(type, "onWorldsUnloading");
        runtimeServerStopped = method(type, "onServerStopped");
        runtimeTick = method(type, "onServerTick", long.class);
        runtimeJoin = method(type, "onPlayerJoin", long.class);
        runtimeWorldChange = method(type, "onPlayerWorldChange", long.class,
                                    int.class, int.class);
        runtimeQuit = method(type, "onPlayerQuit", long.class);
        runtimePlayerConfiguration = method(type, "onPlayerConfiguration", long.class);
        runtimePlayerConfigurationAborted = method(type,
            "onPlayerConfigurationAborted", long.class);
        runtimePluginMessage = method(type, "onPluginMessage", long.class,
            int.class, String.class, byte[].class);
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
        runtimeAllowDeath = method(type, "onEntityAllowDeath", long.class,
                                   long.class, float.class, String.class);
        runtimeDamageApplied = method(type, "onEntityDamageApplied", long.class,
                                      long.class, float.class, float.class,
                                      boolean.class, boolean.class, String.class);
        runtimeSpawn = method(type, "onMobSpawn", long.class, double.class,
                              double.class, double.class, int.class);
        runtimeMobLoad = method(type, "onMobLoad", long.class, int.class);
        runtimeMobUnload = method(type, "onMobUnload", long.class, int.class);
        runtimeDataPackReload = method(type, "onDataPackReload", Collection.class);
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
    private static void copyOfficialShadow(Path source, Path destination,
                                           Set<String> officialFabricClasses) throws IOException {
        try (var stream = Files.walk(source)) {
            for (Path item : (Iterable<Path>) stream::iterator) {
                if (Files.isSymbolicLink(item)) continue;
                Path relative = source.relativize(item);
                if (excludedOfficialShadow(relative, officialFabricClasses)) continue;
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

    private static boolean excludedOfficialShadow(Path relative,
                                                   Set<String> officialFabricClasses) {
        String value = relative.toString().replace(File.separatorChar, '/');
        return value.startsWith("net/fabricmc/api/")
            || value.startsWith("net/fabricmc/loader/")
            || (isFabricApiOrImplClass(value) && officialFabricClasses.contains(value))
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

    /**
     * Return exact API/implementation class paths supplied by installed mods,
     * including nested jars. Knot resolves the earlier shadow URL first, so
     * only matching shadow classes are omitted; unmatched runtime shims remain.
     */
    private static Set<String> fabricApiClasses(Path modsDirectory) throws IOException {
        Set<String> classes = new LinkedHashSet<>();
        if (!Files.isDirectory(modsDirectory)) return classes;
        List<Path> candidates;
        try (var paths = Files.list(modsDirectory)) {
            candidates = paths.sorted(Comparator.comparing(Path::toString)).toList();
        }
        for (Path candidate : candidates) {
            if (Files.isSymbolicLink(candidate)) continue;
            if (Files.isDirectory(candidate)) {
                try (var paths = Files.walk(candidate)) {
                    for (Path path : (Iterable<Path>) paths::iterator) {
                        if (!Files.isRegularFile(path)) continue;
                        if (path.getFileName().toString().endsWith(".jar")) {
                            classes.addAll(jarFabricApiClasses(path));
                        } else {
                            String relative = candidate.relativize(path).toString()
                                .replace(File.separatorChar, '/');
                            if (isFabricApiOrImplClass(relative)) classes.add(relative);
                        }
                    }
                }
            } else if (Files.isRegularFile(candidate)
                    && candidate.getFileName().toString().endsWith(".jar")) {
                classes.addAll(jarFabricApiClasses(candidate));
            }
        }
        return classes;
    }

    private static Set<String> jarFabricApiClasses(Path jarPath) throws IOException {
        Set<String> classes = new LinkedHashSet<>();
        try (JarFile jar = new JarFile(jarPath.toFile())) {
            List<java.util.jar.JarEntry> nestedJars = new ArrayList<>();
            var entries = jar.entries();
            while (entries.hasMoreElements()) {
                var entry = entries.nextElement();
                if (isFabricApiOrImplClass(entry.getName())) classes.add(entry.getName());
                String name = entry.getName();
                if (!entry.isDirectory() && name.startsWith("META-INF/jars/")
                        && name.endsWith(".jar")) {
                    nestedJars.add(entry);
                }
            }
            for (var nestedJar : nestedJars) {
                try (var input = jar.getInputStream(nestedJar);
                     var nested = new ZipInputStream(input)) {
                    ZipEntry entry;
                    while ((entry = nested.getNextEntry()) != null) {
                        if (isFabricApiOrImplClass(entry.getName())) classes.add(entry.getName());
                    }
                }
            }
        }
        return classes;
    }

    private static boolean isFabricApiOrImplClass(String entryName) {
        return (entryName.startsWith("net/fabricmc/fabric/api/")
                || entryName.startsWith("net/fabricmc/fabric/impl/"))
            && entryName.endsWith(".class");
    }

    private static ClassLoader makeLoader(URL[] urls) throws ReflectiveOperationException {
        boolean strict = Boolean.getBoolean("cppfm.jvm.strict");
        for (String name : List.of("cppfm.loader.KnotClassLoader",
                                  "cppfm.loader.KnotLikeClassLoader")) {
            try {
                Class<?> type = Class.forName(name, true, KnotLauncher.class.getClassLoader());
                try {
                    Constructor<?> constructor = type.getConstructor(
                        URL[].class, ClassLoader.class, boolean.class);
                    return (ClassLoader) constructor.newInstance(
                        urls, KnotLauncher.class.getClassLoader(), strict);
                } catch (NoSuchMethodException ignored) {
                    Constructor<?> constructor = type.getConstructor(URL[].class, ClassLoader.class);
                    ClassLoader result = (ClassLoader) constructor.newInstance(
                        urls, KnotLauncher.class.getClassLoader());
                    // Third-party compatible providers may expose a mutable
                    // strict switch instead of the three-argument
                    // constructor.  Propagate the policy when available so
                    // --jvm-strict cannot silently degrade to best effort.
                    try {
                        Method setter = type.getMethod("setStrict", boolean.class);
                        setter.invoke(result, strict);
                    } catch (NoSuchMethodException ignoredSetter) {
                        // An older provider has no strict policy surface.
                    }
                    return result;
                }
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
        for (Path candidate : modPaths(modsDir))
            resources.addAll(mixinResources(candidate));
        for (String resource : resources) register.invoke(target, resource);
    }

    /**
     * Register mod-declared Access Wideners before mixin classes or target
     * classes are resolved.  This is kept separate from mixin discovery so a
     * mod with no mixins still receives its access flags in the production
     * fallback loader.
     */
    private static void prepareAccessWideners(ClassLoader target, String modsDir) throws Exception {
        Method register;
        try { register = target.getClass().getMethod("registerAccessWidener", String.class); }
        catch (NoSuchMethodException ignored) { return; }
        Set<String> resources = new LinkedHashSet<>();
        for (Path candidate : modPaths(modsDir))
            resources.addAll(accessWidenerResources(candidate));
        for (String resource : resources) register.invoke(target, resource);
    }

    @SuppressWarnings("unchecked")
    private static List<String> accessWidenerResources(Path candidate) throws Exception {
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
        Object value = map.get("accessWidener");
        if (value instanceof String name && !name.isBlank()) return List.of(name);
        return List.of();
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

    private static List<Path> modPaths(String modsDir) throws IOException {
        if (fallbackModExpansion != null) return fallbackModExpansion.paths();
        if (modsDir == null || modsDir.isBlank()) return List.of();
        Path directory = Paths.get(modsDir);
        if (!Files.isDirectory(directory)) return List.of();
        try (var stream = Files.list(directory)) {
            return stream
                .filter(item -> Files.isDirectory(item) || item.toString().endsWith(".jar"))
                .sorted(Comparator.comparing(Path::toString)).toList();
        }
    }

    private static List<URL> urls(String classesDir, String modsDir) throws IOException {
        List<URL> result = new ArrayList<>();
        List<URL> classes = new ArrayList<>();
        Path classesPath = null;
        if (classesDir != null && !classesDir.isBlank()) {
            classesPath = Paths.get(classesDir).toAbsolutePath().normalize();
            if (!Files.isDirectory(classesPath))
                throw new IOException("classes directory is missing: " + classesPath);
            classes.add(classesPath.toUri().toURL());
        }
        // Put real server libraries ahead of compile-time ABI shims. The
        // official Fabric host uses the same dependency set so API classes
        // and mods cannot resolve different copies of Guava/Gson/etc.
        for (Path library : libraryPaths()) result.add(library.toUri().toURL());
        result.addAll(classes);
        // Dependency-free builds keep compile-only ABI implementations out of
        // the production class tree.  Make that fallback explicit by adding
        // the sibling directory when it exists; a supplied real library set
        // remains first in the URL order and therefore wins for Gson/Netty,
        // DFU, logging, and the other external namespaces.
        if (classesPath != null) {
            Path compileStubs = classesPath.getParent() == null ? null
                : classesPath.getParent().resolve("compile-stubs");
            if (compileStubs != null && Files.isDirectory(compileStubs))
                result.add(compileStubs.toUri().toURL());
        }
        if (modsDir != null && !modsDir.isBlank()) {
            Path path = Paths.get(modsDir);
            if (Files.isDirectory(path)) {
                try (var stream = Files.list(path)) {
                    List<Path> candidates = stream
                        .filter(item -> Files.isDirectory(item) || item.toString().endsWith(".jar"))
                        .sorted(Comparator.comparing(Path::toString)).toList();
                    fallbackModExpansion = NestedJarSupport.expand(candidates);
                    for (Path item : fallbackModExpansion.paths())
                        result.add(item.toUri().toURL());
                } catch (UrlFailure failure) { throw failure.io; }
            }
        }
        return result;
    }

    private static List<Path> libraryPaths() throws IOException {
        String librariesDir = System.getProperty("cppfm.jvm.libraries");
        if (librariesDir == null || librariesDir.isBlank()) return List.of();
        Path root = Paths.get(librariesDir).toAbsolutePath().normalize();
        if (!Files.isDirectory(root))
            throw new IOException("JVM libraries directory is missing: " + root);
        try (var stream = Files.list(root)) {
            return stream
                .filter(item -> Files.isDirectory(item) || item.toString().endsWith(".jar"))
                // Fabric Loader/Mixin ship the pinned ASM 9.7.1 family. The
                // vanilla 1.21.4 library set also contains ASM 9.6; exposing
                // both to Knot makes its classpath verifier reject startup.
                .filter(item -> !isPinnedAsmOverride(item))
                .sorted(Comparator.comparing(Path::toString)).toList();
        }
    }

    private static boolean isPinnedAsmOverride(Path library) {
        if (Files.isDirectory(library)) return false;
        String name = library.getFileName().toString().toLowerCase(Locale.ROOT);
        return name.startsWith("asm-") || name.contains("-asm-");
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
        runtimeServerShutdown = null;
        runtimeServerStopping = null;
        runtimeServerWorldsLoaded = null;
        runtimeWorldsUnloading = null;
        runtimeServerStopped = null;
        runtimeTick = null;
        runtimeJoin = null;
        runtimeWorldChange = null;
        runtimeQuit = null;
        runtimePlayerConfiguration = null;
        runtimePlayerConfigurationAborted = null;
        runtimePluginMessage = null;
        runtimeChat = null;
        runtimeBlockBreak = null;
        runtimeBlockPlace = null;
        runtimeBlockClicked = null;
        runtimeCommand = null;
        runtimeDamage = null;
        runtimeAllowDeath = null;
        runtimeDamageApplied = null;
        runtimeSpawn = null;
        runtimeMobLoad = null;
        runtimeMobUnload = null;
        runtimeDataPackReload = null;
        started = false;
        bootstrapping = false;
        knotProviderSelected = false;
        runtimeCalls = 0;
        shutdownInProgress = false;
        shutdownCleanupPending = false;
        shutdownCallbackComplete = false;
        shutdownFinalizerThread = null;
        savedOfficialProperties.clear();
    }

    /** Surface fail-closed transformer decisions in the native evidence log. */
    private static void logTransformerDiagnostics() {
        if (loader == null) return;
        try {
            try {
                Method applied = loader.getClass().getMethod("getAppliedMixins");
                Object value = applied.invoke(loader);
                if (value instanceof Iterable<?> entries) {
                    for (Object entry : entries)
                        NativeBridge.logFallback("INFO", String.valueOf(entry));
                }
            } catch (NoSuchMethodException ignored) {
                // Older compatibility loaders have no success-evidence API.
            }
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
        try {
            if (loader instanceof URLClassLoader urls && loader != officialHost) {
                try { urls.close(); }
                catch (IOException failure) {
                    NativeBridge.logFallback("WARN", "Knot classloader close failed: " + failure);
                }
            }
        } finally {
            if (fallbackModExpansion != null) {
                fallbackModExpansion.close();
                fallbackModExpansion = null;
            }
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
