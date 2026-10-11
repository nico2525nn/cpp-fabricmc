package cppfm.bridge;

import cppfm.api.ModEvents;
import java.io.IOException;
import java.io.InputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Comparator;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executor;
import java.util.concurrent.ForkJoinPool;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Function;
import java.util.stream.Stream;

import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.api.ModInitializer;
import net.fabricmc.fabric.api.command.v2.CommandRegistrationCallback;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.entity.event.v1.ServerPlayerEvents;
import net.fabricmc.fabric.api.entity.event.v1.ServerEntityWorldChangeEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerWorldEvents;
import net.fabricmc.fabric.api.event.player.AttackBlockCallback;
import net.fabricmc.fabric.api.event.player.PlayerBlockBreakEvents;
import net.fabricmc.fabric.api.event.player.UseBlockCallback;
import net.fabricmc.fabric.api.event.player.UseEntityCallback;
import net.fabricmc.fabric.api.event.player.UseItemCallback;
import net.fabricmc.fabric.api.message.v1.ServerMessageEvents;
import net.fabricmc.fabric.api.networking.v1.ServerConfigurationConnectionEvents;
import net.fabricmc.fabric.api.networking.v1.ServerConfigurationNetworking;
import net.fabricmc.fabric.api.networking.v1.ServerPlayConnectionEvents;
import net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking;
import net.fabricmc.fabric.api.resource.IdentifiableResourceReloadListener;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.block.BlockState;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.network.message.MessageType;
import net.minecraft.network.message.SignedMessage;
import net.minecraft.resource.LifecycledResourceManager;
import net.minecraft.resource.ResourceReloader;
import net.minecraft.resource.ResourceType;
import net.minecraft.registry.RegistryWrapper;
import net.minecraft.server.MinecraftServer;
import net.minecraft.network.ClientConnection;
import net.minecraft.server.network.ConnectedClientData;
import net.minecraft.server.network.ServerConfigurationNetworkHandler;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.ActionResult;
import net.minecraft.util.Hand;
import net.minecraft.util.Identifier;
import net.minecraft.util.NativeAccess;
import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.util.hit.EntityHitResult;
import net.minecraft.util.math.BlockPos;

/**
 * Java-side lifecycle and compatibility loader for the embedded runtime.
 *
 * The bootstrap is intentionally dependency-free so cppfm can start and test
 * the boundary without bundling third-party binaries.  If a full Fabric Loader
 * distribution is supplied later, this class is the narrow hand-off point for
 * that provider; the C++ authoritative state and callback contract remain the
 * same.
 */
public final class CppModRuntime {
    /** Native onCommand transport marker: the suffix is console feedback. */
    private static final String HANDLED_COMMAND_PREFIX = "\u0001cppfm-handled:";
    private static final List<ServerReloadRegistration> SERVER_RELOAD_LISTENERS = new CopyOnWriteArrayList<>();
    private static final AtomicBoolean SERVER_RELOAD_IN_PROGRESS = new AtomicBoolean();
    private static final Executor RESOURCE_RELOAD_WORKERS = ForkJoinPool.commonPool();
    private static final Set<Long> LOADED_PLAYER_HANDLES = ConcurrentHashMap.newKeySet();
    private static final Map<Long, ServerConfigurationNetworkHandler> CONFIGURATION_HANDLERS =
        new ConcurrentHashMap<>();
    private static final Map<String, String> savedLoaderProperties = new LinkedHashMap<>();
    private static ClassLoader modLoader;
    private static ThreadContextClassLoaderLease modContextClassLoaderLease;
    private static NestedJarSupport.Expansion modExpansion;
    /** Immutable, safely-published lifecycle view read once by each callback. */
    private static final class RuntimeState {
        private static final RuntimeState STOPPED = new RuntimeState(null, false, false);

        private final MinecraftServer server;
        private final boolean bootstrapped;
        private final boolean serverWorldsLoaded;

        private RuntimeState(MinecraftServer server, boolean bootstrapped,
                             boolean serverWorldsLoaded) {
            this.server = server;
            this.bootstrapped = bootstrapped;
            this.serverWorldsLoaded = serverWorldsLoaded;
        }
    }

    private static volatile RuntimeState runtimeState = RuntimeState.STOPPED;
    private static final Object SHUTDOWN_LOCK = new Object();
    // Stage values: 0=running, 1=stopping, 2=worlds unloaded, 3=stopped.
    // State is claimed under SHUTDOWN_LOCK, but mod callbacks run outside it.
    private static int shutdownLifecycleStage;
    private static int shutdownCallbacksInFlight;
    private static int asynchronousReloadsInFlight;
    private static boolean shutdownCleanupRequested;
    private static boolean shutdownCleanupStarted;
    private static boolean shutdownFinalizeClaimed;
    // Guarded by SHUTDOWN_LOCK; callbacks never run while this lock is held.
    private static boolean bootstrapInProgress;
    private CppModRuntime() {}

    /** One acquire-read for the full runtime view used by a callback. */
    private static RuntimeState activeRuntime() {
        RuntimeState state = runtimeState;
        return state.bootstrapped ? state : null;
    }

    private static void publishLoadingServer(MinecraftServer current) {
        synchronized (SHUTDOWN_LOCK) {
            if (!bootstrapInProgress || shutdownCleanupRequested)
                throw new IllegalStateException("server shutdown requested during mod bootstrap");
            runtimeState = new RuntimeState(current, false, false);
        }
    }

    public static boolean bootstrap(String modsDir, String configDir) {
        synchronized (SHUTDOWN_LOCK) {
            if (runtimeState.bootstrapped) return true;
            if (bootstrapInProgress || shutdownCleanupStarted || shutdownCallbacksInFlight != 0)
                return false;
            bootstrapInProgress = true;
            shutdownLifecycleStage = 0;
            asynchronousReloadsInFlight = 0;
            shutdownCleanupRequested = false;
            shutdownCleanupStarted = false;
            shutdownFinalizeClaimed = false;
            runtimeState = RuntimeState.STOPPED;
        }
        try {
            loadMods(Paths.get(modsDir));
            RuntimeState loadingState = runtimeState;
            MinecraftServer current = loadingState.server;
            if (current == null) {
                current = MinecraftServer.of(NativeAccess.serverHandle());
                publishLoadingServer(current);
            }
            MinecraftServer activeServer = current;
            synchronized (SHUTDOWN_LOCK) {
                if (!bootstrapInProgress || shutdownCleanupRequested)
                    throw new IllegalStateException("server shutdown requested during mod bootstrap");
                runtimeState = new RuntimeState(activeServer, true, false);
            }
            invokeSafely(() -> ServerLifecycleEvents.SERVER_STARTING.invoker()
                .onServerStarting(activeServer), "server starting");
            boolean cleanupRequested;
            synchronized (SHUTDOWN_LOCK) {
                bootstrapInProgress = false;
                cleanupRequested = shutdownCleanupRequested;
            }
            if (cleanupRequested) {
                drainShutdownAndCleanup();
                return false;
            }
            return true;
        } catch (Throwable failure) {
            Throwable cause = failure;
            while ((cause instanceof InvocationTargetException
                    || cause instanceof ExceptionInInitializerError)
                   && cause.getCause() != null)
                cause = cause.getCause();
            clearRuntime();
            StringWriter details = new StringWriter();
            cause.printStackTrace(new PrintWriter(details));
            log("ERROR", "mod bootstrap failed:\n" + details);
            return false;
        }
    }

    /** Completes the startup phases after native worlds have loaded. */
    public static void onServerWorldsLoaded() {
        MinecraftServer current;
        synchronized (SHUTDOWN_LOCK) {
            RuntimeState state = runtimeState;
            if (!state.bootstrapped || state.server == null || state.serverWorldsLoaded ||
                bootstrapInProgress || shutdownLifecycleStage != 0 || shutdownCallbacksInFlight != 0)
                return;
            current = state.server;
            runtimeState = new RuntimeState(current, true, true);
            shutdownCallbacksInFlight++;
        }
        try {
            current.adoptCurrentThread();
            current.finishWorldLoad();
            fireWorldLoad(current);
            invokeSafely(() -> ServerLifecycleEvents.SERVER_STARTED.invoker()
                .onServerStarted(current), "server started");
        } finally {
            completeShutdownStage();
        }
    }

    /** Fabric SERVER_STOPPING: the network and worlds remain available. */
    public static void onServerStopping() {
        dispatchShutdownStage(1);
    }

    /** Fabric ServerWorldEvents.UNLOAD, once for each live server world. */
    public static void onWorldsUnloading() {
        RuntimeState runtime = claimShutdownStage(2);
        if (runtime == null) return;
        try {
            MinecraftServer current = runtime.server;
            if (current != null) {
                for (ServerWorld world : current.getWorlds()) {
                    if (world == null) continue;
                    invokeSafely(() -> ServerWorldEvents.UNLOAD.invoker()
                        .onWorldUnload(current, world),
                        "world unload " + world.getRegistryKey().getValue());
                }
                current.finishWorldUnload();
            }
        } finally {
            completeShutdownStage();
        }
    }

    /** Fabric SERVER_STOPPED: all worlds have been closed and detached. */
    public static void onServerStopped() {
        dispatchShutdownStage(3);
    }

    /** Idempotent compatibility entrypoint that advances every remaining stage. */
    public static void onServerShutdown() {
        onServerStopping();
        onWorldsUnloading();
        onServerStopped();
    }

    private static void dispatchShutdownStage(int stage) {
        RuntimeState runtime = claimShutdownStage(stage);
        if (runtime == null) return;
        MinecraftServer current = runtime.server;
        try {
            if (stage == 1) {
                current.beginShutdown();
                invokeSafely(() -> ServerLifecycleEvents.SERVER_STOPPING.invoker()
                    .onServerStopping(current), "server stopping");
            } else if (stage == 3) {
                invokeSafely(() -> ServerLifecycleEvents.SERVER_STOPPED.invoker()
                    .onServerStopped(current), "server stopped");
            }
        } finally {
            completeShutdownStage();
        }
    }

    private static RuntimeState claimShutdownStage(int stage) {
        synchronized (SHUTDOWN_LOCK) {
            RuntimeState state = runtimeState;
            if (!state.bootstrapped || state.server == null || bootstrapInProgress ||
                shutdownCallbacksInFlight != 0 ||
                shutdownLifecycleStage + 1 != stage)
                return null;
            shutdownLifecycleStage = stage;
            shutdownCallbacksInFlight = 1;
            return state;
        }
    }

    private static void completeShutdownStage() {
        boolean resumeShutdown;
        synchronized (SHUTDOWN_LOCK) {
            shutdownCallbacksInFlight = Math.max(0, shutdownCallbacksInFlight - 1);
            resumeShutdown = shutdownCleanupRequested &&
                shutdownCallbacksInFlight == 0 && !shutdownCleanupStarted;
        }
        if (resumeShutdown) drainShutdownAndCleanup();
    }

    private static void drainShutdownAndCleanup() {
        synchronized (SHUTDOWN_LOCK) {
            shutdownCleanupRequested = true;
            if (shutdownCleanupStarted || bootstrapInProgress || shutdownCallbacksInFlight != 0) return;
            shutdownCleanupStarted = true;
        }
        onServerStopping();
        onWorldsUnloading();
        onServerStopped();
        boolean finalizeRuntime;
        synchronized (SHUTDOWN_LOCK) {
            if (shutdownCallbacksInFlight != 0) {
                shutdownCleanupStarted = false;
                return;
            }
            if (asynchronousReloadsInFlight != 0 || shutdownFinalizeClaimed) return;
            shutdownFinalizeClaimed = true;
            finalizeRuntime = true;
        }
        if (finalizeRuntime) clearRuntime();
    }

    public static void shutdown() {
        RuntimeState state = runtimeState;
        MinecraftServer current = state.server;
        if (current != null) current.beginShutdown();
        // Native teardown normally dispatched every stage at its real server
        // boundary. This fallback also covers direct Java shutdown and partial
        // bootstrap; it never holds the class monitor across mod code.
        drainShutdownAndCleanup();
    }

    private static void clearRuntime() {
        synchronized (SHUTDOWN_LOCK) {
            runtimeState = RuntimeState.STOPPED;
        }
        Throwable cleanupFailure = null;
        cleanupFailure = attemptCleanup(cleanupFailure, WrapperCache::clear);
        cleanupFailure = attemptCleanup(cleanupFailure,
            CppModRuntime::clearConfigurationHandlers);
        cleanupFailure = attemptCleanup(cleanupFailure, CppModRuntime::clearRegistrations);
        cleanupFailure = attemptCleanup(cleanupFailure, CppModRuntime::restoreLoaderMetadata);
        cleanupFailure = attemptCleanup(cleanupFailure, MixinHooks::clear);
        cleanupFailure = attemptCleanup(cleanupFailure, ModEvents::clearAll);
        cleanupFailure = attemptCleanup(cleanupFailure, CppModRuntime::clearLoaderRuntime);
        cleanupFailure = attemptCleanup(cleanupFailure, CppModRuntime::closeModLoader);
        synchronized (SHUTDOWN_LOCK) {
            runtimeState = RuntimeState.STOPPED;
            shutdownLifecycleStage = 0;
            shutdownCallbacksInFlight = 0;
            asynchronousReloadsInFlight = 0;
            shutdownCleanupRequested = false;
            shutdownCleanupStarted = false;
            shutdownFinalizeClaimed = false;
            bootstrapInProgress = false;
        }
        if (cleanupFailure != null) {
            StringWriter details = new StringWriter();
            cleanupFailure.printStackTrace(new PrintWriter(details));
            log("ERROR", "JVM runtime cleanup had failures:\n" + details);
        }
    }

    private static Throwable attemptCleanup(Throwable previous, Runnable cleanup) {
        try {
            cleanup.run();
        } catch (Throwable failure) {
            if (previous == null) return failure;
            if (failure != previous) previous.addSuppressed(failure);
        }
        return previous;
    }

    public static void onServerTick(long tick) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null) return;
        MinecraftServer current = runtime.server;
        if (current != null) current.adoptCurrentThread();
        // JvmRuntime dispatches a transformed setTick(J)V body before this
        // facade.  Do not call it a second time: a second call would execute
        // every injected handler twice.  With no transformed route this is
        // the historical shadow/native path and remains unchanged; the
        // NativeAccess fallback returns zero for javac-only execution.
        if (current != null && NativeAccess.routePath(
                "net/minecraft/server/MinecraftServer", "setTick", "(J)V") == 0)
            current.setTick(tick);
        for (ModEvents.Tick callback : ModEvents.TICK.snapshot())
            invokeSafely(() -> callback.onTick(current, tick), "cppfm tick");
        invokeSafely(() -> ServerTickEvents.START_SERVER_TICK.invoker()
            .onStartTick(current), "server tick start (fabric)");
        if (current != null) {
            for (ServerWorld world : current.getWorlds()) {
                invokeSafely(() -> ServerTickEvents.START_WORLD_TICK.invoker()
                    .onStartTick(world), "world tick start");
            }
        }
        if (current != null) {
            for (ServerWorld world : current.getWorlds()) {
                invokeSafely(() -> ServerTickEvents.END_WORLD_TICK.invoker()
                    .onEndTick(world), "world tick end");
            }
        }
        invokeSafely(() -> ServerTickEvents.END_SERVER_TICK.invoker()
            .onEndTick(current), "server tick end (fabric)");
        if (current != null) current.runTasksTillTickEnd();
    }

    public static void onPlayerJoin(long handle) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null || handle == 0L) return;
        finishPlayerConfiguration(handle);
        MinecraftServer current = runtime.server;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerPlayNetworkHandler network = player.getNetworkHandler();
        invokeSafely(() -> ServerPlayConnectionEvents.JOIN.invoker()
            .onPlayReady(network, ServerPlayNetworking.getSender(player), current), "player join");
        if (network.isDisconnected()) return;
        invokeOptionalEvent("net.fabricmc.fabric.api.entity.event.v1.ServerPlayerEvents",
            "JOIN", "onPlayReady",
            new Class<?>[] {ServerPlayNetworkHandler.class, ServerPlayerEntity.class},
            "player entity join", network, player);
        if (network.isDisconnected()) return;
        invokeSafely(() -> ServerLifecycleEvents.SYNC_DATA_PACK_CONTENTS.invoker()
            .onSyncDataPackContents(player, true), "data-pack sync");
        if (network.isDisconnected()) return;
        if (LOADED_PLAYER_HANDLES.add(handle))
            dispatchEntityLoad(runtime, player, player.getServerWorld());
    }

    /** Publishes a native portal transfer through Fabric's player event. */
    public static void onPlayerWorldChange(long handle, int sourceDimension,
                                           int destinationDimension) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null || handle == 0L ||
                sourceDimension == destinationDimension) return;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        if (player == null) return;
        ServerWorld origin = ServerWorld.of(
            NativeAccess.serverWorld(sourceDimension), runtime.server);
        ServerWorld destination = ServerWorld.of(
            NativeAccess.serverWorld(destinationDimension), runtime.server);
        if (origin.nativeHandle() == 0L || destination.nativeHandle() == 0L)
            return;

        // The native transfer has committed before this callback. Refresh the
        // Java wrapper first so Fabric listeners observe the destination pose.
        player.worldChanged(destination);
        player.refreshPositionAndAngles(
            player.getX(), player.getY(), player.getZ(),
            player.getYaw(), player.getPitch());
        invokeSafely(() -> ServerEntityWorldChangeEvents.AFTER_PLAYER_CHANGE_WORLD
            .invoker().afterChangeWorld(player, origin, destination),
            "player world change");
    }

    public static void onPlayerQuit(long handle) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null || handle == 0L) return;
        finishPlayerConfiguration(handle);
        MinecraftServer current = runtime.server;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerPlayNetworkHandler network = player.getNetworkHandler();
        // The native session owns the socket lifecycle. Reflect its completed
        // disconnect into the Java handler before Fabric's Disconnect event,
        // whose contract forbids packet sends from callbacks.
        network.onDisconnected(null);
        invokeOptionalEvent("net.fabricmc.fabric.api.entity.event.v1.ServerPlayerEvents",
            "LEAVE", "onPlayDisconnect",
            new Class<?>[] {ServerPlayNetworkHandler.class, ServerPlayerEntity.class},
            "player entity leave", network, player);
        invokeSafely(() -> ServerPlayConnectionEvents.DISCONNECT.invoker()
            .onPlayDisconnect(network, current), "player quit");
        if (LOADED_PLAYER_HANDLES.remove(handle))
            dispatchEntityUnload(runtime, player, player.getServerWorld());
        WrapperCache.remove(handle);
    }

    /** Opens the Fabric configuration-phase handler before its first packet. */
    public static void onPlayerConfiguration(long handle) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null || handle == 0L) return;
        ServerConfigurationNetworkHandler handler = configurationHandler(
            runtime.server, handle);
        if (handler == null) return;
        invokeSafely(() -> ServerConfigurationConnectionEvents.BEFORE_CONFIGURE
            .invoker().onSendConfiguration(handler, runtime.server),
            "before player configuration");
        invokeSafely(() -> ServerConfigurationConnectionEvents.CONFIGURE
            .invoker().onSendConfiguration(handler, runtime.server),
            "player configuration");
    }

    /** Releases phase-local networking state when configuration is abandoned. */
    public static void onPlayerConfigurationAborted(long handle) {
        RuntimeState runtime = activeRuntime();
        ServerConfigurationNetworkHandler handler = CONFIGURATION_HANDLERS.remove(handle);
        if (handler == null) return;
        if (runtime != null) {
            invokeSafely(() -> ServerConfigurationConnectionEvents.DISCONNECT
                .invoker().onConfigureDisconnect(handler, runtime.server),
                "configuration disconnect");
        }
        clearConfigurationReceivers(handler);
        handler.endConfiguration();
        WrapperCache.remove(handle);
    }

    private static ServerConfigurationNetworkHandler configurationHandler(
            MinecraftServer server, long handle) {
        return CONFIGURATION_HANDLERS.computeIfAbsent(handle, key ->
            new ServerConfigurationNetworkHandler(server,
                new ClientConnection(key, 0), new ConnectedClientData()));
    }

    private static void finishPlayerConfiguration(long handle) {
        ServerConfigurationNetworkHandler handler =
            CONFIGURATION_HANDLERS.remove(handle);
        if (handler == null) return;
        clearConfigurationReceivers(handler);
        handler.endConfiguration();
    }

    private static void clearConfigurationReceivers(
            ServerConfigurationNetworkHandler handler) {
        for (Identifier id : ServerConfigurationNetworking.getReceived(handler))
            ServerConfigurationNetworking.unregisterReceiver(handler, id);
    }

    private static void clearConfigurationHandlers() {
        for (ServerConfigurationNetworkHandler handler : CONFIGURATION_HANDLERS.values()) {
            try {
                clearConfigurationReceivers(handler);
                handler.endConfiguration();
            } catch (Throwable failure) {
                log("WARN", "could not clear a pending configuration handler: " + failure);
            }
        }
        CONFIGURATION_HANDLERS.clear();
    }

    /** Dispatches the Fabric data-pack reload-start lifecycle hook. */
    public static void onDataPackReloadStart(LifecycledResourceManager resourceManager) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null) return;
        dispatchDataPackReloadStart(runtime, resourceManager);
    }

    private static void dispatchDataPackReloadStart(RuntimeState runtime,
                                                    LifecycledResourceManager resourceManager) {
        MinecraftServer current = runtime.server;
        invokeSafely(() -> ServerLifecycleEvents.START_DATA_PACK_RELOAD.invoker()
            .startDataPackReload(current, resourceManager), "data-pack reload start");
    }

    /** Dispatches the Fabric data-pack reload-end lifecycle hook. */
    public static void onDataPackReloadEnd(LifecycledResourceManager resourceManager,
                                           boolean success) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null) return;
        dispatchDataPackReloadEnd(runtime, resourceManager, success);
    }

    private static void dispatchDataPackReloadEnd(RuntimeState runtime,
                                                  LifecycledResourceManager resourceManager,
                                                  boolean success) {
        MinecraftServer current = runtime.server;
        invokeSafely(() -> ServerLifecycleEvents.END_DATA_PACK_RELOAD.invoker()
            .endDataPackReload(current, resourceManager, success), "data-pack reload end");
    }

    /** Starts an asynchronous server-data reload on the server owner thread. */
    public static CompletableFuture<Void> onDataPackReload() {
        return onDataPackReload(activeRuntime(), null);
    }

    /**
     * Starts a reload requested through MinecraftServer.reloadResources.
     * This bridge has no ResourcePackManager profile-to-pack binding, so it
     * fails closed for named selections rather than silently loading another
     * pack set.
     */
    public static CompletableFuture<Void> onDataPackReload(Collection<String> dataPacks) {
        if (dataPacks == null) return CompletableFuture.failedFuture(
            new IllegalArgumentException("data-pack selection must not be null"));
        final List<String> selectedDataPacks;
        try {
            selectedDataPacks = List.copyOf(dataPacks);
        } catch (RuntimeException invalidSelection) {
            return CompletableFuture.failedFuture(new IllegalArgumentException(
                "data-pack selection is invalid", invalidSelection));
        }
        if (!selectedDataPacks.isEmpty()) {
            return CompletableFuture.failedFuture(new UnsupportedOperationException(
                "server resource reload cannot honor selected data packs yet: "
                    + String.join(",", selectedDataPacks)));
        }
        return onDataPackReload(activeRuntime(), selectedDataPacks);
    }

    private static CompletableFuture<Void> onDataPackReload(
            RuntimeState runtime, Collection<String> selectedDataPacks) {
        if (runtime == null || runtime.server == null)
            return CompletableFuture.failedFuture(
                new IllegalStateException("server mod runtime is not active"));
        if (!runtime.server.isOnThread())
            return CompletableFuture.failedFuture(new IllegalStateException(
                "resource reload must be initiated on the MinecraftServer owner thread"));

        synchronized (SHUTDOWN_LOCK) {
            if (runtimeState != runtime || !runtime.bootstrapped || shutdownCleanupRequested ||
                !SERVER_RELOAD_IN_PROGRESS.compareAndSet(false, true))
                return CompletableFuture.failedFuture(
                    new IllegalStateException("server resource reload is unavailable or in progress"));
            asynchronousReloadsInFlight++;
        }

        final LifecycledResourceManager resourceManager;
        try {
            resourceManager = selectedDataPacks == null || selectedDataPacks.isEmpty()
                ? createServerResourceManager() : new LifecycledResourceManager();
        } catch (Throwable failure) {
            releaseResourceReloadLease();
            return CompletableFuture.failedFuture(failure);
        }
        ReloadOperation operation = new ReloadOperation(runtime, resourceManager);
        operation.start();
        return operation.result;
    }

    private static void releaseResourceReloadLease() {
        boolean finalizeShutdown = false;
        synchronized (SHUTDOWN_LOCK) {
            asynchronousReloadsInFlight = Math.max(0, asynchronousReloadsInFlight - 1);
            SERVER_RELOAD_IN_PROGRESS.set(false);
            if (shutdownCleanupRequested && shutdownCleanupStarted &&
                shutdownCallbacksInFlight == 0 && asynchronousReloadsInFlight == 0 &&
                !shutdownFinalizeClaimed) {
                shutdownFinalizeClaimed = true;
                finalizeShutdown = true;
            }
        }
        if (finalizeShutdown) clearRuntime();
    }

    /**
     * Coordinates one nonblocking reload. All prepare stages enter a shared
     * barrier; listener dependency edges gate apply completion; only then are
     * the end event and manager close permitted.
     */
    private static final class ReloadOperation {
        private final RuntimeState runtime;
        private final MinecraftServer server;
        private final LifecycledResourceManager manager;
        private final CompletableFuture<Void> result = new CompletableFuture<>();
        private final CompletableFuture<Void> allPrepared = new CompletableFuture<>();
        private final AtomicInteger preparedCount = new AtomicInteger();
        private final AtomicBoolean finishQueued = new AtomicBoolean();
        private final AtomicBoolean finished = new AtomicBoolean();
        private final AtomicReference<Throwable> firstFailure = new AtomicReference<>();
        private final Map<Identifier, ListenerState> listeners = new LinkedHashMap<>();
        private volatile boolean stopRequested;
        private volatile RuntimeException stopFailure;
        private volatile boolean startDispatched;
        private Runnable unregisterStopCallback = () -> { };

        private ReloadOperation(RuntimeState runtime, LifecycledResourceManager manager) {
            this.runtime = runtime;
            this.server = runtime.server;
            this.manager = manager;
        }

        private void start() {
            unregisterStopCallback = server.addStopCallback(this::serverStopping);
            if (stopRequested) {
                finishWithoutOwner(stopFailure);
                return;
            }
            try {
                dispatchDataPackReloadStart(runtime, manager);
                startDispatched = true;
                ReloadPlan resolved = resolveServerReloadListeners();
                ReloadPlan ordered = orderServerReloadListeners(resolved.listeners);
                if (!resolved.success || !ordered.success) {
                    recordFailure(new IllegalStateException(
                        "server reload listener resolution or dependency ordering failed"));
                    queueFinish();
                    return;
                }

                for (ResolvedReloadListener listener : ordered.listeners)
                    listeners.put(listener.id, new ListenerState(listener));
                if (listeners.isEmpty()) allPrepared.complete(null);

                ClassLoader contextLoader = Thread.currentThread().getContextClassLoader();
                Executor prepareExecutor = command -> RESOURCE_RELOAD_WORKERS.execute(
                    withContextLoader(contextLoader, command));
                for (ListenerState state : listeners.values()) startListener(
                    state, contextLoader, prepareExecutor);

                CompletableFuture<?>[] completions = listeners.values().stream()
                    .map(state -> state.settled)
                    .toArray(CompletableFuture<?>[]::new);
                CompletableFuture.allOf(completions).whenComplete(
                    (ignored, failure) -> queueFinish());
            } catch (Throwable failure) {
                recordFailure(failure);
                queueFinish();
            }
        }

        private void startListener(ListenerState state, ClassLoader contextLoader,
                                   Executor prepareExecutor) {
            ResourceReloader.Synchronizer synchronizer = new ResourceReloader.Synchronizer() {
                @Override
                public <T> CompletableFuture<T> whenPrepared(T preparedObject) {
                    if (!state.prepared.compareAndSet(false, true)) {
                        IllegalStateException duplicate = new IllegalStateException(
                            "listener entered reload preparation barrier more than once: " +
                                state.listener.id);
                        state.fail(duplicate);
                        return CompletableFuture.failedFuture(duplicate);
                    }
                    if (preparedCount.incrementAndGet() == listeners.size())
                        allPrepared.complete(null);
                    return readyAfterDependencies(state).thenApply(ignored -> preparedObject);
                }
            };

            Executor serverExecutor = server.resourceReloadExecutor(state::fail);
            Executor applyExecutor = command -> serverExecutor.execute(
                withContextLoader(contextLoader, command));
            try {
                CompletableFuture<Void> future = state.listener.listener.reload(
                    synchronizer, manager, prepareExecutor, applyExecutor);
                if (future == null) {
                    state.fail(new IllegalStateException(
                        "listener returned null reload future: " + state.listener.id));
                    return;
                }
                future.whenComplete((ignored, failure) -> {
                    if (failure != null) {
                        Throwable cause = unwrapReloadFailure(failure);
                        if (!allPrepared.isDone()) allPrepared.completeExceptionally(cause);
                        state.fail(cause);
                    } else if (!state.prepared.get()) {
                        IllegalStateException missingBarrier = new IllegalStateException(
                            "listener completed without entering preparation barrier: " +
                                state.listener.id);
                        allPrepared.completeExceptionally(missingBarrier);
                        state.fail(missingBarrier);
                    } else {
                        readyAfterDependencies(state).whenComplete((ready, readinessFailure) -> {
                            if (readinessFailure == null) state.settled.complete(null);
                            else state.fail(unwrapReloadFailure(readinessFailure));
                        });
                    }
                });
            } catch (Throwable failure) {
                Throwable cause = unwrapReloadFailure(failure);
                if (!allPrepared.isDone()) allPrepared.completeExceptionally(cause);
                state.fail(cause);
            }
        }

        private CompletableFuture<Void> readyAfterDependencies(ListenerState state) {
            return allPrepared.thenCompose(ignored -> {
                CompletableFuture<?>[] dependencies = state.listener.dependencies.stream()
                    .map(listeners::get)
                    .filter(Objects::nonNull)
                    .map(dependency -> dependency.settled)
                    .toArray(CompletableFuture<?>[]::new);
                return CompletableFuture.allOf(dependencies);
            });
        }

        private void queueFinish() {
            if (!finishQueued.compareAndSet(false, true)) return;
            server.executeResourceReloadTask(this::finishOnOwner, this::finishWithoutOwner);
        }

        private void serverStopping(RuntimeException failure) {
            stopFailure = failure;
            stopRequested = true;
        }

        private void finishOnOwner() {
            if (!server.isOnThread()) {
                finishWithoutOwner(new IllegalStateException(
                    "resource reload finalization escaped MinecraftServer owner thread"));
                return;
            }
            if (stopRequested || server.isStopping()) {
                finishWithoutOwner(stopFailure != null ? stopFailure :
                    new IllegalStateException("server stopped during resource reload"));
                return;
            }
            Throwable failure = firstFailure.get();
            boolean success = failure == null;
            if (startDispatched) dispatchDataPackReloadEnd(runtime, manager, success);
            finishResources(failure, false);
        }

        private void finishWithoutOwner(RuntimeException rejection) {
            if (rejection != null) {
                stopFailure = rejection;
                stopRequested = true;
            }
            Throwable failure = firstFailure.get();
            finishResources(failure != null ? failure : stopFailure, true);
        }

        private void finishResources(Throwable failure, boolean skippedEndEvent) {
            if (!finished.compareAndSet(false, true)) return;
            try {
                manager.close();
            } catch (Throwable closeFailure) {
                if (failure == null) failure = closeFailure;
                else if (failure != closeFailure) failure.addSuppressed(closeFailure);
                logReloadFailure("server resource reload resource manager close", closeFailure);
            } finally {
                try { unregisterStopCallback.run(); }
                catch (Throwable ignored) { }
                releaseResourceReloadLease();
            }
            if (failure == null && !stopRequested) {
                log("INFO", "server resource reload complete success=true listeners=" +
                    listeners.size());
                result.complete(null);
            } else {
                if (failure == null) failure = new IllegalStateException(
                    "server stopped during resource reload");
                log("INFO", "server resource reload " +
                    (skippedEndEvent ? "aborted" : "complete") +
                    " success=false listeners=" + listeners.size());
                result.completeExceptionally(failure);
            }
        }

        private void recordFailure(Throwable failure) {
            recordFailure("server resource reload", failure);
        }

        private void recordFailure(String operation, Throwable failure) {
            Throwable cause = unwrapReloadFailure(failure);
            firstFailure.compareAndSet(null, cause);
            logReloadFailure(operation, cause);
        }

        private static Runnable withContextLoader(ClassLoader loader, Runnable command) {
            return () -> {
                Thread thread = Thread.currentThread();
                ClassLoader previous = thread.getContextClassLoader();
                try {
                    if (loader != null) thread.setContextClassLoader(loader);
                    command.run();
                } finally {
                    if (loader != null) thread.setContextClassLoader(previous);
                }
            };
        }

        private final class ListenerState {
            private final ResolvedReloadListener listener;
            private final AtomicBoolean prepared = new AtomicBoolean();
            private final AtomicBoolean failed = new AtomicBoolean();
            private final CompletableFuture<Void> settled = new CompletableFuture<>();

            private ListenerState(ResolvedReloadListener listener) {
                this.listener = listener;
            }

            private void fail(Throwable failure) {
                Throwable cause = unwrapReloadFailure(failure);
                if (!failed.compareAndSet(false, true)) return;
                if (!allPrepared.isDone()) allPrepared.completeExceptionally(cause);
                recordFailure("server resource reload listener " + listener.id, cause);
                settled.completeExceptionally(cause);
            }
        }
    }

    private static Throwable unwrapReloadFailure(Throwable failure) {
        Throwable cause = failure;
        while ((cause instanceof java.util.concurrent.CompletionException ||
                cause instanceof java.util.concurrent.ExecutionException ||
                cause instanceof InvocationTargetException) && cause.getCause() != null)
            cause = cause.getCause();
        return cause;
    }

    /** Dispatches the Fabric pre-save lifecycle hook at the native save boundary. */
    public static void onBeforeSave(boolean flush, boolean skipErrors) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null) return;
        MinecraftServer current = runtime.server;
        invokeSafely(() -> ServerLifecycleEvents.BEFORE_SAVE.invoker()
            .onBeforeSave(current, flush, skipErrors), "before save");
    }

    /** Dispatches the Fabric post-save lifecycle hook at the native save boundary. */
    public static void onAfterSave(boolean flush, boolean skipErrors) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null) return;
        MinecraftServer current = runtime.server;
        invokeSafely(() -> ServerLifecycleEvents.AFTER_SAVE.invoker()
            .onAfterSave(current, flush, skipErrors), "after save");
    }

    /** Return null to cancel, otherwise return the possibly rewritten message. */
    public static String onChat(long handle, String message) {
        if (activeRuntime() == null) return message;
        String current = message;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        SignedMessage signed = SignedMessage.of(message);
        if (!invokeAllow(() -> ServerMessageEvents.ALLOW_CHAT_MESSAGE.invoker().allowChatMessage(
                signed, player, MessageType.Parameters.EMPTY), "allow chat message")) return null;
        for (ModEvents.Chat callback : ModEvents.CHAT.snapshot()) {
            final String input = current;
            current = invokeResult(() -> callback.onChat(player, input), "cppfm chat");
            if (current == null) return null;
        }
        SignedMessage delivered = SignedMessage.of(current);
        invokeSafely(() -> ServerMessageEvents.CHAT_MESSAGE.invoker()
            .onChatMessage(delivered, player, MessageType.Parameters.EMPTY), "chat message");
        return current;
    }

    public static boolean onBlockBreak(long handle, int x, int y, int z, int rawState) {
        if (activeRuntime() == null) return true;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerWorld world = player.getServerWorld();
        BlockPos pos = new BlockPos(x, y, z);
        BlockState state = new BlockState(rawState);
        if (!invokeAllow(() -> PlayerBlockBreakEvents.BEFORE.invoker()
                .beforeBlockBreak(world, player, pos, state, null), "before block break")) return false;
        for (ModEvents.BlockBreak callback : ModEvents.BLOCK_BREAK.snapshot()) {
            if (!callback.onBlockBreak(player, world, pos, state)) return false;
        }
        return true;
    }

    public static boolean onBlockPlace(long handle, int x, int y, int z, int rawState) {
        if (activeRuntime() == null) return true;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerWorld world = player.getServerWorld();
        BlockPos pos = new BlockPos(x, y, z);
        BlockState state = new BlockState(rawState);
        ActionResult useResult = invokeResult(() -> UseBlockCallback.EVENT.invoker()
            .interact(player, world, Hand.MAIN_HAND, new BlockHitResult(pos)), "use block");
        if (useResult != null && useResult != ActionResult.PASS) return false;
        for (ModEvents.BlockPlace callback : ModEvents.BLOCK_PLACE.snapshot())
            if (!callback.onBlockPlace(player, world, pos, state)) return false;
        return true;
    }

    public static boolean onBlockClicked(long handle, int x, int y, int z,
                                         int rawState, int face) {
        if (activeRuntime() == null) return true;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerWorld world = player.getServerWorld();
        BlockPos pos = new BlockPos(x, y, z);
        BlockState state = new BlockState(rawState);
        ActionResult attackResult = invokeResult(() -> AttackBlockCallback.EVENT.invoker()
            .interact(player, world, Hand.MAIN_HAND, pos,
                net.minecraft.util.math.Direction.byId(face)), "attack block");
        if (attackResult != null && attackResult != ActionResult.PASS) return false;
        for (ModEvents.BlockClicked callback : ModEvents.BLOCK_CLICKED.snapshot())
            if (!callback.onBlockClicked(player, world, pos, state, face)) return false;
        return true;
    }

    /** Return null to cancel, otherwise return the possibly rewritten command. */
    public static String onCommand(long handle, String command) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null) return command;
        MinecraftServer currentServer = runtime.server;
        ServerPlayerEntity player = handle == 0 ? null : ServerPlayerEntity.of(handle);
        String current = command;
        for (ModEvents.Command callback : ModEvents.COMMAND.snapshot()) {
            final String input = current;
            current = invokeResult(() -> callback.onCommand(player, input), "cppfm command");
            if (current == null) return null;
        }
        SignedMessage commandMessage = SignedMessage.of(current);
        net.minecraft.server.command.ServerCommandSource fabricSource =
            new net.minecraft.server.command.ServerCommandSource(player, currentServer);
        if (!invokeAllow(() -> ServerMessageEvents.ALLOW_COMMAND_MESSAGE.invoker()
                .allowCommandMessage(commandMessage, fabricSource, MessageType.Parameters.EMPTY),
                "allow command message")) return null;
        if ("reload".equals(current.trim())
                && (currentServer == null || !currentServer.getCommandManager().hasCommand(current))) {
            onDataPackReload(runtime, null).whenComplete((ignored, failure) -> {
                log("INFO", "server /reload Java listeners success=" +
                    (failure == null));
            });
        }
        // Commands registered through Fabric's CommandRegistrationCallback
        // must be consumed here, otherwise the native command dispatcher
        // would run a second, unrelated command tree.
        if (currentServer != null && currentServer.getCommandManager().hasCommand(current)) {
            StringBuilder commandResponse = new StringBuilder();
            net.minecraft.server.command.ServerCommandSource commandSource = new net.minecraft.server.command.ServerCommandSource(
                message -> {
                    if (message == null) return;
                    if (player != null) {
                        player.sendMessage(message);
                    } else {
                        if (commandResponse.length() > 0) commandResponse.append('\n');
                        commandResponse.append(message.getString());
                    }
                },
                player == null ? net.minecraft.util.math.Vec3d.ZERO : player.getPos(),
                net.minecraft.util.math.Vec2f.ZERO,
                player == null ? currentServer.getOverworld() : player.getServerWorld(),
                4,
                player == null ? "Console" : player.getName().getString(),
                player == null ? net.minecraft.text.Text.literal("Console") : player.getName(),
                currentServer,
                player);
            try {
                currentServer.getCommandManager().getDispatcher().execute(current, commandSource);
            } catch (Throwable failure) {
                StringWriter details = new StringWriter();
                failure.printStackTrace(new PrintWriter(details));
                log("ERROR", "command execution failed:\n" + details);
                if (commandResponse.length() == 0) {
                    commandResponse.append("error: ").append(failure.getMessage() == null
                        ? failure.getClass().getSimpleName() : failure.getMessage());
                }
            }
            invokeSafely(() -> ServerMessageEvents.COMMAND_MESSAGE.invoker()
                .onCommandMessage(commandMessage, commandSource, MessageType.Parameters.EMPTY), "command message");
            return HANDLED_COMMAND_PREFIX + commandResponse;
        }
        return current;
    }

    public static boolean onEntityDamage(long playerHandle, long entityHandle,
                                         float amount, String cause) {
        if (activeRuntime() == null) return true;
        LivingEntity victim = playerHandle != 0 ? ServerPlayerEntity.of(playerHandle) : LivingEntity.of(entityHandle);
        if (victim == null) return true;
        DamageSource source = new DamageSource(cause == null ? "generic" : cause);
        if (!invokeAllow(() -> net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents
                .ALLOW_DAMAGE.invoker().allowDamage(victim, source, amount), "allow damage")) return false;
        if (!invokeAllow(() -> net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents
                .ALLOW_DAMAGE.invoker().allowDamage(victim, source, amount), "allow damage (legacy)")) return false;
        for (ModEvents.EntityDamage callback : ModEvents.ENTITY_DAMAGE.snapshot())
            if (!invokeAllow(() -> callback.onEntityDamage(victim, amount, cause), "cppfm damage")) return false;
        return true;
    }

    /**
     * Decide whether a lethal native damage application may kill its victim.
     * Any Fabric ALLOW_DEATH listener returning false vetoes the lethal
     * transition; native combat must keep the victim alive and report
     * {@code died=false} to the post-damage hook. Callback exceptions are
     * logged and treated as allow so they cannot abort JNI.
     */
    public static boolean onEntityAllowDeath(long playerHandle, long entityHandle,
                                              float amount, String cause) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null) return true;
        LivingEntity victim = playerHandle != 0L
            ? ServerPlayerEntity.of(playerHandle) : LivingEntity.of(entityHandle);
        return dispatchEntityAllowDeath(runtime, victim, amount, cause);
    }

    /** Shared event dispatcher; native handle adaptation remains at the JNI boundary. */
    public static boolean dispatchEntityAllowDeath(LivingEntity victim,
                                                    float amount,
                                                    String cause) {
        return dispatchEntityAllowDeath(activeRuntime(), victim, amount, cause);
    }

    private static boolean dispatchEntityAllowDeath(RuntimeState runtime,
                                                     LivingEntity victim,
                                                     float amount,
                                                     String cause) {
        if (runtime == null) return true;
        if (victim == null) return true;
        DamageSource source = new DamageSource(cause == null ? "generic" : cause);
        if (!invokeAllow(() -> net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents
                .ALLOW_DEATH.invoker().allowDeath(victim, source, amount), "allow death")) return false;
        if (!invokeAllow(() -> net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents
                .ALLOW_DEATH.invoker().allowDeath(victim, source, amount), "allow death (legacy)")) return false;
        if (victim instanceof ServerPlayerEntity player) {
            if (!invokeAllow(() -> ServerPlayerEvents.ALLOW_DEATH.invoker()
                    .allowDeath(player, source, amount), "player allow death")) return false;
        }
        return true;
    }

    /** Dispatch the post-commit Fabric damage callbacks from native combat. */
    public static void onEntityDamageApplied(long playerHandle, long entityHandle,
                                              float baseDamage, float damageTaken,
                                              boolean blocked, boolean died, String cause) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null) return;
        LivingEntity victim = playerHandle != 0 ? ServerPlayerEntity.of(playerHandle) : LivingEntity.of(entityHandle);
        dispatchEntityDamageApplied(runtime, victim, baseDamage, damageTaken,
                                    blocked, died, cause);
    }

    /** Shared post-damage dispatcher used by the JNI adapter and API fixture. */
    public static void dispatchEntityDamageApplied(LivingEntity victim,
                                                    float baseDamage,
                                                    float damageTaken,
                                                    boolean blocked,
                                                    boolean died,
                                                    String cause) {
        dispatchEntityDamageApplied(activeRuntime(), victim, baseDamage, damageTaken,
                                    blocked, died, cause);
    }

    private static void dispatchEntityDamageApplied(RuntimeState runtime,
                                                    LivingEntity victim,
                                                    float baseDamage,
                                                    float damageTaken,
                                                    boolean blocked,
                                                    boolean died,
                                                    String cause) {
        if (runtime == null) return;
        if (victim == null) return;
        DamageSource source = new DamageSource(cause == null ? "generic" : cause);
        if (died) {
            invokeSafely(() -> net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents
                .AFTER_DEATH.invoker().afterDeath(victim, source), "after death");
            invokeSafely(() -> net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents
                .AFTER_DEATH.invoker().afterDeath(victim, source), "after death (legacy)");
        } else if (victim.isAlive()) {
            // A canceled ALLOW_DEATH transition does not necessarily restore
            // positive health. Match LivingEntity.damage: zero-health vetoes
            // remain dead and do not receive AFTER_DAMAGE; listeners that
            // restore health before vetoing do receive it.
            invokeSafely(() -> net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents
                .AFTER_DAMAGE.invoker().afterDamage(victim, source, baseDamage, damageTaken, blocked),
                "after damage");
            invokeSafely(() -> net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents
                .AFTER_DAMAGE.invoker().afterDamage(victim, source, baseDamage, damageTaken, blocked),
                "after damage (legacy)");
        }
    }

    /** Compatibility for the older native call shape; current native damage is unblocked. */
    public static void onEntityDamageApplied(long playerHandle, long entityHandle,
                                              float amount, String cause, boolean died) {
        onEntityDamageApplied(playerHandle, entityHandle, amount, amount, false, died, cause);
    }

    public static boolean onMobSpawn(long entityHandle, double x, double y,
                                     double z, int dimension) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null) return true;
        if (entityHandle == 0L) return false;
        List<ModEvents.MobSpawn> callbacks = ModEvents.MOB_SPAWN.snapshot();
        if (callbacks.isEmpty()) return true;
        Entity entity = Entity.of(entityHandle);
        if (entity == null) return false;
        MinecraftServer current = runtime.server;
        ServerWorld world = current == null ? null : ServerWorld.of(
            NativeAccess.serverWorld(dimension), current);
        if (world != null) entity.setWorld(world);
        for (ModEvents.MobSpawn callback : callbacks) {
            if (!invokeAllow(() -> callback.onMobSpawn(entity, world, x, y, z), "mob spawn")) {
                NativeAccess.beginEntityRetirement(entityHandle);
                WrapperCache.remove(entityHandle);
                return false;
            }
        }
        return true;
    }

    public static void onMobLoad(long entityHandle, int dimension) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null || entityHandle == 0L) return;
        Entity entity = Entity.of(entityHandle);
        MinecraftServer current = runtime.server;
        ServerWorld world = current == null ? null : ServerWorld.of(
            NativeAccess.serverWorld(dimension), current);
        if (entity == null || world == null) return;
        entity.setWorld(world);
        dispatchEntityLoad(runtime, entity, world);
    }

    public static void onMobUnload(long entityHandle, int dimension) {
        if (entityHandle == 0L) return;
        try {
            RuntimeState runtime = activeRuntime();
            if (runtime == null) return;
            Entity entity = Entity.of(entityHandle);
            MinecraftServer current = runtime.server;
            ServerWorld world = current == null ? null : ServerWorld.of(
                NativeAccess.serverWorld(dimension), current);
            if (entity == null || world == null) return;
            entity.setWorld(world);
            dispatchEntityUnload(runtime, entity, world);
        } finally {
            // Keep native-backed methods usable for the unload listener, then
            // evict the strong Java identity wrapper before its native handle
            // is invalidated by JvmRuntime.
            WrapperCache.remove(entityHandle);
        }
    }

    public static ActionResult onEntityUse(long playerHandle, long entityHandle, Hand hand) {
        if (activeRuntime() == null || playerHandle == 0L || entityHandle == 0L) return ActionResult.PASS;
        ServerPlayerEntity player = ServerPlayerEntity.of(playerHandle);
        if (player == null) return ActionResult.PASS;
        ServerWorld world = player.getServerWorld();
        Entity entity = Entity.of(entityHandle);
        if (entity == null) return ActionResult.PASS;
        return invokeResult(() -> UseEntityCallback.EVENT.invoker().interact(player, world,
            hand == null ? Hand.MAIN_HAND : hand, entity, new EntityHitResult(entity)), "use entity");
    }

    public static ActionResult onItemUse(long playerHandle, Hand hand) {
        if (activeRuntime() == null) return ActionResult.PASS;
        ServerPlayerEntity player = ServerPlayerEntity.of(playerHandle);
        return invokeResult(() -> UseItemCallback.EVENT.invoker().interact(player, player.getServerWorld(),
            hand == null ? Hand.MAIN_HAND : hand), "use item");
    }

    /** Native play/config plugin-message entrypoint (JILjava/lang/String;[B)V. */
    public static void onPluginMessage(long handle, int phase, String channel, byte[] payload) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null || channel == null) return;
        MinecraftServer current = runtime.server;
        try {
            byte[] safePayload = payload == null ? new byte[0] : payload.clone();
            if (phase == 0) {
                ServerConfigurationNetworkHandler handler =
                    configurationHandler(current, handle);
                FabricNetworkingDispatch.dispatchConfiguration(channel,
                    safePayload, current, handler);
            } else if (phase == 1) {
                ServerPlayerEntity player = ServerPlayerEntity.of(handle);
                ServerPlayNetworkHandler handler = player == null
                    ? null : player.getNetworkHandler();
                FabricNetworkingDispatch.dispatchPlay(channel, safePayload,
                    current, player, handler);
            } else {
                throw new IllegalArgumentException(
                    "unsupported custom payload phase: " + phase);
            }
        } catch (Throwable failure) {
            // Do not leave a Java exception pending across the native void
            // callback boundary when an optional receiver fails.
            log("ERROR", "plugin message receiver failed (phase " + phase + "): " + failure);
        }
    }

    public static void onBlockBreakResult(long handle, int x, int y, int z, int rawState,
                                          boolean broken) {
        if (activeRuntime() == null) return;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerWorld world = player.getServerWorld();
        BlockPos pos = new BlockPos(x, y, z);
        BlockState state = new BlockState(rawState);
        if (broken) invokeSafely(() -> PlayerBlockBreakEvents.AFTER.invoker()
            .afterBlockBreak(world, player, pos, state, null), "after block break");
        else invokeSafely(() -> PlayerBlockBreakEvents.CANCELED.invoker()
            .onBlockBreakCanceled(world, player, pos, state, null), "canceled block break");
    }

    public static void onPlayerRespawn(long oldHandle, long newHandle, boolean alive) {
        if (activeRuntime() == null) return;
        ServerPlayerEntity oldPlayer = ServerPlayerEntity.of(oldHandle);
        ServerPlayerEntity newPlayer = ServerPlayerEntity.of(newHandle);
        invokeSafely(() -> ServerPlayerEvents.COPY_FROM.invoker()
            .copyFromPlayer(oldPlayer, newPlayer, alive), "player copy");
        invokeSafely(() -> ServerPlayerEvents.AFTER_RESPAWN.invoker()
            .afterRespawn(oldPlayer, newPlayer, alive), "player respawn");
    }

    public static boolean onGameMessage(long playerHandle, String message, boolean overlay) {
        RuntimeState runtime = activeRuntime();
        if (runtime == null) return true;
        MinecraftServer current = runtime.server;
        ServerPlayerEntity player = playerHandle == 0L ? null : ServerPlayerEntity.of(playerHandle);
        net.minecraft.text.Text text = net.minecraft.text.Text.literal(message == null ? "" : message);
        if (!invokeAllow(() -> ServerMessageEvents.ALLOW_GAME_MESSAGE.invoker()
                .allowGameMessage(current, text, overlay), "allow game message")) return false;
        invokeSafely(() -> ServerMessageEvents.GAME_MESSAGE.invoker()
            .onGameMessage(current, text, overlay), "game message");
        return true;
    }

    public static void dispatchEntityLoad(Entity entity, ServerWorld world) {
        dispatchEntityLoad(activeRuntime(), entity, world);
    }

    private static void dispatchEntityLoad(RuntimeState runtime, Entity entity, ServerWorld world) {
        if (runtime == null || entity == null) return;
        invokeSafely(() -> net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents
            .ENTITY_LOAD.invoker().onLoad(entity, world), "entity load");
        invokeSafely(() -> net.fabricmc.fabric.api.entity.event.v1.ServerEntityEvents
            .LOAD.invoker().onLoad(entity, world), "entity load (cppfm extension)");
    }

    public static void dispatchEntityUnload(Entity entity, ServerWorld world) {
        dispatchEntityUnload(activeRuntime(), entity, world);
    }

    private static void dispatchEntityUnload(RuntimeState runtime, Entity entity, ServerWorld world) {
        if (runtime == null || entity == null) return;
        invokeSafely(() -> net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents
            .ENTITY_UNLOAD.invoker().onUnload(entity, world), "entity unload");
        invokeSafely(() -> net.fabricmc.fabric.api.entity.event.v1.ServerEntityEvents
            .UNLOAD.invoker().onUnload(entity, world), "entity unload (cppfm extension)");
    }

    public static void registerServerReloadListener(IdentifiableResourceReloadListener listener) {
        Objects.requireNonNull(listener, "listener");
        Identifier id = Objects.requireNonNull(listener.getFabricId(), "listener.fabricId");
        registerServerReloadListener(new ServerReloadRegistration(id, listener, null));
    }

    public static void registerServerReloadListener(
            Identifier identifier,
            Function<RegistryWrapper.WrapperLookup, IdentifiableResourceReloadListener> factory) {
        Objects.requireNonNull(identifier, "identifier");
        Objects.requireNonNull(factory, "factory");
        registerServerReloadListener(new ServerReloadRegistration(identifier, null, factory));
    }

    private static void registerServerReloadListener(ServerReloadRegistration registration) {
        synchronized (SERVER_RELOAD_LISTENERS) {
            for (ServerReloadRegistration existing : SERVER_RELOAD_LISTENERS) {
                if (existing.id.equals(registration.id))
                    throw new IllegalArgumentException(
                        "duplicate server resource reload listener: " + registration.id);
            }
            SERVER_RELOAD_LISTENERS.add(registration);
        }
    }

    private static void clearRegistrations() {
        SERVER_RELOAD_LISTENERS.clear();
        LOADED_PLAYER_HANDLES.clear();
        SERVER_RELOAD_IN_PROGRESS.set(false);
        clearFabricEventRegistrations();
        for (String className : List.of(
                "net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking",
                "net.fabricmc.fabric.api.itemgroup.v1.ItemGroupEvents",
                "net.fabricmc.fabric.api.attachment.v1.AttachmentRegistry"))
            invokeOptionalStaticCleanup(className);
        restoreLoaderMetadata();
    }

    private static void clearFabricEventRegistrations() {
        ClassLoader loader = CppModRuntime.class.getClassLoader();
        for (String className : List.of(
                "net.fabricmc.fabric.api.command.v2.CommandRegistrationCallback",
                "net.fabricmc.fabric.api.entity.event.v1.ServerEntityEvents",
                "net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents",
                "net.fabricmc.fabric.api.entity.event.v1.ServerPlayerEvents",
                "net.fabricmc.fabric.api.entity.event.v1.EntityElytraEvents",
                "net.fabricmc.fabric.api.entity.event.v1.EntitySleepEvents",
                "net.fabricmc.fabric.api.entity.event.v1.ServerEntityCombatEvents",
                "net.fabricmc.fabric.api.entity.event.v1.ServerEntityWorldChangeEvents",
                "net.fabricmc.fabric.api.event.lifecycle.v1.CommonLifecycleEvents",
                "net.fabricmc.fabric.api.event.lifecycle.v1.ServerBlockEntityEvents",
                "net.fabricmc.fabric.api.event.lifecycle.v1.ServerChunkEvents",
                "net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents",
                "net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents",
                "net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents",
                "net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents",
                "net.fabricmc.fabric.api.event.lifecycle.v1.ServerWorldEvents",
                "net.fabricmc.fabric.api.event.player.AttackBlockCallback",
                "net.fabricmc.fabric.api.event.player.AttackEntityCallback",
                "net.fabricmc.fabric.api.event.player.PlayerBlockBreakEvents",
                "net.fabricmc.fabric.api.event.player.PlayerPickItemEvents",
                "net.fabricmc.fabric.api.event.player.UseBlockCallback",
                "net.fabricmc.fabric.api.event.player.UseEntityCallback",
                "net.fabricmc.fabric.api.event.player.UseItemCallback",
                "net.fabricmc.fabric.api.message.v1.ServerMessageEvents",
                "net.fabricmc.fabric.api.networking.v1.EntityTrackingEvents",
                "net.fabricmc.fabric.api.networking.v1.S2CConfigurationChannelEvents",
                "net.fabricmc.fabric.api.networking.v1.S2CPlayChannelEvents",
                "net.fabricmc.fabric.api.networking.v1.ServerConfigurationConnectionEvents",
                "net.fabricmc.fabric.api.networking.v1.ServerLoginConnectionEvents",
                "net.fabricmc.fabric.api.networking.v1.ServerPlayConnectionEvents")) {
            try {
                Class<?> owner = Class.forName(className, true, loader);
                for (java.lang.reflect.Field field : owner.getFields()) {
                    if (!java.lang.reflect.Modifier.isStatic(field.getModifiers())
                            || !Event.class.isAssignableFrom(field.getType())) continue;
                    Object event = field.get(null);
                    Method clear = findClearMethod(event.getClass());
                    if (clear == null || !clear.trySetAccessible()) continue;
                    try {
                        clear.invoke(event);
                    } catch (InvocationTargetException failure) {
                        log("WARN", "could not reset Fabric shadow event " + className + "."
                            + field.getName() + ": " + failure.getCause());
                    }
                }
            } catch (ClassNotFoundException missingOptionalApi) {
                // Fabric API modules are optional in the dependency-free path.
            } catch (ReflectiveOperationException | LinkageError | RuntimeException failure) {
                log("WARN", "could not inspect Fabric event class " + className + ": " + failure);
            }
        }
    }

    private static Method findClearMethod(Class<?> type) {
        for (Class<?> current = type; current != null; current = current.getSuperclass()) {
            try { return current.getDeclaredMethod("clear"); }
            catch (NoSuchMethodException ignored) { }
        }
        return null;
    }

    private static void invokeOptionalStaticCleanup(String className) {
        try {
            Class<?> type = Class.forName(className, true, CppModRuntime.class.getClassLoader());
            Method cleanup = type.getMethod("clear");
            if (!java.lang.reflect.Modifier.isStatic(cleanup.getModifiers())) return;
            cleanup.invoke(null);
        } catch (ClassNotFoundException | NoSuchMethodException missingOptionalApi) {
            // The official API owns its event state and does not expose cppfm reset hooks.
        } catch (InvocationTargetException failure) {
            log("WARN", "optional Fabric cleanup failed for " + className + ": " + failure.getCause());
        } catch (ReflectiveOperationException | LinkageError | RuntimeException failure) {
            log("WARN", "optional Fabric cleanup failed for " + className + ": " + failure);
        }
    }

    private static LifecycledResourceManager createServerResourceManager() {
        LinkedHashSet<Path> roots = new LinkedHashSet<>();
        Path workingDirectory = Paths.get(System.getProperty("user.dir", "."));
        addResourceRoot(roots, workingDirectory.resolve("assets"));
        try {
            addResourceRoot(roots, FabricLoader.getInstance().getGameDir().resolve("assets"));
        } catch (Throwable failure) {
            logReloadFailure("server resource reload game directory lookup", failure);
        }

        String worldSetting = NativeAccess.serverSetting("world-dir");
        if (worldSetting != null && !worldSetting.isBlank()) {
            try {
                addDataPackRoots(roots, Paths.get(worldSetting).toAbsolutePath().normalize());
            } catch (java.nio.file.InvalidPathException failure) {
                log("WARN", "server resource reload ignored invalid world directory: " + worldSetting);
            }
        }
        if (modExpansion != null) {
            for (Path path : modExpansion.paths()) addResourceRoot(roots, path);
        }
        return LifecycledResourceManager.fromPaths(ResourceType.SERVER_DATA, roots);
    }

    private static void addResourceRoot(Set<Path> roots, Path root) {
        if (root == null) return;
        try { roots.add(root.toAbsolutePath().normalize()); }
        catch (java.nio.file.InvalidPathException ignored) { }
    }

    private static void addDataPackRoots(Set<Path> roots, Path worldDirectory) {
        Path datapacks = worldDirectory.resolve("datapacks");
        if (!Files.isDirectory(datapacks)) return;
        try (var stream = Files.list(datapacks)) {
            stream.filter(path -> Files.isDirectory(path) || Files.isRegularFile(path))
                .sorted(Comparator.comparing(Path::toString))
                .forEach(path -> addResourceRoot(roots, path));
        } catch (IOException failure) {
            logReloadFailure("server resource reload data-pack discovery", failure);
        }
    }

    private static ReloadPlan resolveServerReloadListeners() {
        List<ResolvedReloadListener> listeners = new ArrayList<>();
        boolean success = true;
        RegistryWrapper.WrapperLookup lookup = RegistryWrapper.WrapperLookup.of(Stream.empty());
        for (ServerReloadRegistration registration : snapshot(SERVER_RELOAD_LISTENERS)) {
            try {
                IdentifiableResourceReloadListener listener = registration.listener != null
                    ? registration.listener : registration.factory.apply(lookup);
                if (listener == null)
                    throw new IllegalStateException("listener factory returned null");
                Collection<Identifier> declared = listener.getFabricDependencies();
                List<Identifier> dependencies = new ArrayList<>();
                if (declared != null) {
                    for (Identifier dependency : declared)
                        if (dependency != null) dependencies.add(dependency);
                }
                listeners.add(new ResolvedReloadListener(
                    registration.id, listener, List.copyOf(dependencies)));
            } catch (Throwable failure) {
                success = false;
                logReloadFailure("server resource reload listener " + registration.id + " setup",
                                  failure);
            }
        }
        return new ReloadPlan(List.copyOf(listeners), success);
    }

    private static ReloadPlan orderServerReloadListeners(List<ResolvedReloadListener> listeners) {
        Map<Identifier, ResolvedReloadListener> byId = new LinkedHashMap<>();
        for (ResolvedReloadListener listener : listeners) byId.put(listener.id, listener);
        Set<Identifier> visiting = new HashSet<>();
        Set<Identifier> visited = new HashSet<>();
        List<ResolvedReloadListener> ordered = new ArrayList<>();
        boolean success = true;
        for (ResolvedReloadListener listener : listeners) {
            if (!visitReloadListener(listener.id, byId, visiting, visited, ordered))
                success = false;
        }
        return new ReloadPlan(success ? List.copyOf(ordered) : List.of(), success);
    }

    private static boolean visitReloadListener(
            Identifier id,
            Map<Identifier, ResolvedReloadListener> byId,
            Set<Identifier> visiting,
            Set<Identifier> visited,
            List<ResolvedReloadListener> ordered) {
        if (visited.contains(id)) return true;
        if (!visiting.add(id)) {
            log("ERROR", "server resource reload dependency cycle at " + id);
            return false;
        }
        ResolvedReloadListener listener = byId.get(id);
        boolean success = listener != null;
        if (listener != null) {
            for (Identifier dependency : listener.dependencies) {
                if (byId.containsKey(dependency)
                        && !visitReloadListener(dependency, byId, visiting, visited, ordered))
                    success = false;
            }
        }
        visiting.remove(id);
        if (success) {
            visited.add(id);
            ordered.add(listener);
        }
        return success;
    }

    private static void logReloadFailure(String operation, Throwable failure) {
        Throwable cause = failure;
        while ((cause instanceof java.util.concurrent.CompletionException
                    || cause instanceof java.util.concurrent.ExecutionException
                    || cause instanceof InvocationTargetException
                    || cause instanceof ExceptionInInitializerError)
                && cause.getCause() != null)
            cause = cause.getCause();
        StringWriter details = new StringWriter();
        cause.printStackTrace(new PrintWriter(details));
        log("ERROR", operation + " failed:\n" + details);
    }

    /**
     * Register the server-side subset of the Mixin metadata before any mod
     * entrypoint is initialized.  This is deliberately a small execution
     * shell: it loads annotation-bearing classes and routes only the
     * explicitly exposed C++ hook points through MixinHooks.
     */
    private static void registerMixins(List<ModCatalog.Candidate> candidates) throws Exception {
        MixinHooks.clear();
        for (ModCatalog.Candidate candidate : candidates) {
            for (String config : candidate.mixinConfigs()) {
                try (InputStream input = modLoader.getResourceAsStream(config)) {
                    if (input == null) {
                        throw new IOException("mixin config is not on the mod classpath: " + config);
                    }
                    Object parsed = MiniJson.parse(new String(input.readAllBytes(), StandardCharsets.UTF_8));
                    if (!(parsed instanceof Map<?, ?> raw))
                        throw new IllegalArgumentException("mixin config is not an object: " + config);
                    @SuppressWarnings("unchecked") Map<String, Object> json = (Map<String, Object>) raw;
                    String prefix = string(json.get("package"));
                    if (prefix == null) prefix = "";
                    List<String> classNames = new ArrayList<>();
                    addMixinNames(classNames, json.get("mixins"), prefix);
                    // Client mixins must not be loaded by the dedicated
                    // server.  `server` is the standard Fabric selector.
                    addMixinNames(classNames, json.get("server"), prefix);
                    for (String className : classNames) {
                        // Mixin registration must inspect annotations without running
                        // the mixin's static initializer. Shadow fields are bound when
                        // the target class is transformed; initializing the mixin here
                        // would observe them as null.
                        try {
                            Class<?> mixinClass = Class.forName(className, false, modLoader);
                            MixinHooks.registerMixinClass(mixinClass);
                        } catch (Exception failure) {
                            throw new IllegalArgumentException("mixin class " + className +
                                " in " + config + " could not be loaded", failure);
                        }
                    }
                } catch (Exception failure) {
                    log("ERROR", "ignoring invalid mixin config " + config +
                         " from " + candidate.id() + ": " + failure);
                    if (Boolean.getBoolean("cppfm.jvm.strict")) throw failure;
                }
            }
        }
    }

    private static void addMixinNames(List<String> output, Object value, String prefix) {
        if (!(value instanceof List<?> list)) return;
        for (Object entry : list) {
            if (!(entry instanceof String name) || name.isEmpty()) continue;
            String qualified = name.replace('/', '.');
            // Mixin entries are package-relative paths, not Java binary names:
            // names such as "ai.pathing.BlockStateBaseMixin" still belong
            // below the config package.  Avoid adding the prefix twice when a
            // producer has already emitted a qualified name.
            if (!prefix.isEmpty() && !qualified.equals(prefix)
                && !qualified.startsWith(prefix + "."))
                qualified = prefix + "." + qualified;
            output.add(qualified);
        }
    }

    private static String string(Object value) {
        return value instanceof String ? (String) value : null;
    }

    public static void registerTransformedMethod(String owner, String name, String descriptor) {
        NativeBridge.nativeRegisterTransformedMethod(owner, name, descriptor);
    }

    private static void loadMods(Path directory) throws Exception {
        if (!Files.isDirectory(directory)) {
            log("INFO", "mods directory not present: " + directory);
            configureLoaderMetadata(List.of());
            return;
        }
        List<Path> roots = ModCatalog.roots(directory);
        if (modExpansion != null) modExpansion.close();
        modExpansion = NestedJarSupport.expand(roots);

        List<ModCatalog.Candidate> candidates = new ArrayList<>();
        for (Path path : modExpansion.paths()) {
            try {
                ModCatalog.Candidate candidate = ModCatalog.read(path);
                if (candidate == null) continue;
                if (candidate.environmentMatchesServer()) candidates.add(candidate);
                else log("INFO", "skipping client-only mod " + candidate.id());
            } catch (Exception e) {
                log("ERROR", "ignoring invalid mod metadata " + path + ": " + e);
                if (Boolean.getBoolean("cppfm.jvm.strict"))
                    throw new IllegalArgumentException("invalid mod metadata: " + path, e);
            }
        }
        List<ModCatalog.Candidate> order = ModCatalog.dependencyOrder(
            candidates, CppModRuntime::matchesLoaderVersion);

        List<URL> urls = new ArrayList<>();
        for (Path path : modExpansion.paths()) urls.add(path.toUri().toURL());
        // KnotLauncher puts the compatibility classes and mod roots under one
        // child-first loader.  Reusing it is essential: transformed target
        // bytecode and entrypoints must resolve the same mixin class object,
        // not two copies split across sibling URLClassLoaders.  The ordinary
        // invocation fallback still gets an isolated URLClassLoader.
        ClassLoader parent = CppModRuntime.class.getClassLoader();
        if (parent != null && parent.getClass().getName().startsWith("cppfm.loader."))
            modLoader = parent;
        else
            modLoader = new URLClassLoader(urls.toArray(URL[]::new), parent);
        // ServiceLoader-based Fabric mods resolve providers through the
        // thread context loader.  Keep that lookup on the same child-first
        // loader that owns the mod classes, then restore it when the runtime
        // is closed so repeated embedded launches do not leak class-loader
        // identity into the host thread.
        modContextClassLoaderLease =
            ThreadContextClassLoaderLease.install(modLoader);
        int initializedEntrypoints = 0;
        // Mixin metadata must be registered before the first target class is
        // resolved.  The KnotClassLoader created by KnotLauncher has already
        // indexed these configs, so target definitions can be transformed on
        // their first load rather than being patched after the fact.
        configureLoaderMetadata(candidates);
        registerMixins(order);
        // Fabric initializes common entrypoints before dedicated-server
        // entrypoints. Preserve dependency order within each phase while
        // ensuring every mod's main initializer precedes every server one.
        for (String key : List.of("main", "server"))
            for (ModCatalog.Candidate candidate : order)
                initializedEntrypoints += initialize(candidate, key);
        MinecraftServer current = MinecraftServer.of(NativeAccess.serverHandle());
        publishLoadingServer(current);
        invokeSafely(() -> CommandRegistrationCallback.EVENT.invoker()
            .register(current.getCommandManager().getDispatcher(),
                new net.minecraft.command.CommandRegistryAccess(),
                net.minecraft.server.command.CommandManager.RegistrationEnvironment.DEDICATED),
            "command registration");
        try {
            NativeBridge.nativeSetModStats(candidates.size(), initializedEntrypoints);
        } catch (LinkageError | RuntimeException failure) {
            // Mod discovery and initialization remain usable in Java-only
            // fixtures; this native callback publishes diagnostics only.
            log("WARN", "could not publish mod bootstrap stats: " + failure);
        }
        log("INFO", "loaded " + candidates.size() + " mod candidate(s), initialized " + initializedEntrypoints + " entrypoint(s)");
    }

    private static void fireWorldLoad(MinecraftServer current) {
        if (current == null) return;
        for (ServerWorld world : current.getWorlds()) {
            if (world == null) continue;
            invokeSafely(() -> ServerWorldEvents.LOAD.invoker().onWorldLoad(current, world),
                "world load " + world.getRegistryKey().getValue());
        }
    }

    private static void fireWorldUnload() {
        RuntimeState runtime = runtimeState;
        MinecraftServer current = runtime.server;
        ServerWorld world = current == null ? null : current.getOverworld();
        invokeSafely(() -> ServerWorldEvents.UNLOAD.invoker().onWorldUnload(current, world),
            "world unload");
    }

    private static int initialize(ModCatalog.Candidate candidate, String key) throws Exception {
        int initialized = 0;
        for (ModCatalog.Entrypoint entrypointDefinition : candidate.entrypoints()) {
            if (!key.equals(entrypointDefinition.key())) continue;
            String entrypoint = entrypointDefinition.definition();
            String className = entrypoint;
            String methodName = null;
            int separator = entrypoint.indexOf("::");
            if (separator >= 0) {
                className = entrypoint.substring(0, separator);
                methodName = entrypoint.substring(separator + 2);
                if (className.isEmpty() || methodName.isEmpty())
                    throw new IllegalArgumentException("invalid entrypoint: " + entrypoint);
            }
            Class<?> type = Class.forName(className, true, modLoader);
            if (methodName != null) {
                Method method = type.getDeclaredMethod(methodName);
                method.setAccessible(true);
                if (java.lang.reflect.Modifier.isStatic(method.getModifiers()))
                    method.invoke(null);
                else {
                    Object instance = type.getDeclaredConstructor().newInstance();
                    method.invoke(instance);
                    registerLoaderEntrypoint(entrypointDefinition.key(), instance,
                        candidate.id(), entrypoint);
                }
                ++initialized;
                continue;
            }
            Object instance = type.getDeclaredConstructor().newInstance();
            if (instance instanceof DedicatedServerModInitializer dedicated)
                dedicated.onInitializeServer();
            else if (instance instanceof ModInitializer normal)
                normal.onInitialize();
            else {
                Method method = findInitializer(type);
                if (method == null) throw new IllegalArgumentException(entrypoint + " has no supported initializer");
                method.setAccessible(true);
                method.invoke(instance);
            }
            registerLoaderEntrypoint(entrypointDefinition.key(), instance,
                candidate.id(), entrypoint);
            ++initialized;
        }
        return initialized;
    }

    private static Method findInitializer(Class<?> type) {
        for (String name : List.of("onInitializeServer", "onInitialize")) {
            try { return type.getMethod(name); } catch (NoSuchMethodException ignored) {}
        }
        return null;
    }

    private static void configureLoaderMetadata(List<ModCatalog.Candidate> candidates) {
        // The fallback loader exposes these registration hooks, while the
        // opt-in official Loader owns the metadata state itself.  Resolve the
        // optional hooks reflectively so the same CppModRuntime class can be
        // defined by either classloader without a NoSuchMethodError.
        clearLoaderRuntime();
        Path gameDir = FabricLoader.getInstance().getGameDir();
        registerLoaderMod("minecraft", "Minecraft", "1.21.4", gameDir, "*", List.of(),
            Map.of(), Map.of(), Map.of(), Map.of(), Map.of());
        registerLoaderMod("fabricloader", "Fabric Loader", "0.16.9", gameDir, "*", List.of(),
            Map.of(), Map.of(), Map.of(), Map.of(), Map.of());
        LinkedHashMap<String, String> versions = new LinkedHashMap<>();
        for (ModCatalog.Candidate candidate : candidates) {
            versions.put(candidate.id(), candidate.version());
            registerLoaderMod(candidate.id(), candidate.name(), candidate.version(), candidate.path(),
                candidate.environment(), candidate.provides(), candidate.dependencies(),
                candidate.recommends(), candidate.suggests(), candidate.conflicts(), candidate.breaks());
        }
        saveLoaderProperty("cppfm.loaded.mods", String.join(",", versions.keySet()));
        for (Map.Entry<String, String> entry : versions.entrySet()) {
            saveLoaderProperty("cppfm.mod.version." + entry.getKey(), entry.getValue() == null ? "" : entry.getValue());
            ModCatalog.Candidate candidate = candidates.stream()
                .filter(item -> item.id().equals(entry.getKey())).findFirst().orElse(null);
            if (candidate != null) {
                saveLoaderProperty("cppfm.mod.name." + candidate.id(), candidate.name());
                saveLoaderProperty("cppfm.mod.root." + candidate.id(), candidate.path().toString());
                saveLoaderProperty("cppfm.mod.environment." + candidate.id(), candidate.environment());
                saveLoaderProperty("cppfm.mod.provides." + candidate.id(),
                    String.join(",", candidate.provides()));
            }
        }
    }

    private static void saveLoaderProperty(String key, String value) {
        if (!savedLoaderProperties.containsKey(key)) savedLoaderProperties.put(key, System.getProperty(key));
        if (value == null) System.clearProperty(key); else System.setProperty(key, value);
    }

    private static void restoreLoaderMetadata() {
        for (Map.Entry<String, String> entry : savedLoaderProperties.entrySet()) {
            if (entry.getValue() == null) System.clearProperty(entry.getKey());
            else System.setProperty(entry.getKey(), entry.getValue());
        }
        savedLoaderProperties.clear();
    }

    /** Invoke a fallback-only FabricLoader extension when it is present. */
    private static Object invokeOptionalLoaderStatic(String name, Class<?>[] parameterTypes,
                                                     Object... arguments) {
        try {
            Method method = FabricLoader.class.getMethod(name, parameterTypes);
            return method.invoke(null, arguments);
        } catch (NoSuchMethodException ignored) {
            // The official FabricLoader owns this state and intentionally does
            // not expose the fallback runtime's registration helpers.
            return null;
        } catch (InvocationTargetException failure) {
            Throwable cause = failure.getCause();
            if (cause instanceof RuntimeException runtime) throw runtime;
            if (cause instanceof Error error) throw error;
            throw new IllegalStateException("FabricLoader." + name + " failed", cause);
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException("cannot invoke FabricLoader." + name, failure);
        }
    }

    private static void clearLoaderRuntime() {
        invokeOptionalLoaderStatic("clearRuntime", new Class<?>[0]);
    }

    private static void registerLoaderEntrypoint(String key, Object value,
                                                 String modId, String definition) {
        invokeOptionalLoaderStatic("registerEntrypoint",
            new Class<?>[] {String.class, Object.class, String.class, String.class},
            key, value, modId, definition);
    }

    private static void registerLoaderMod(String id, String name, String version, Path root,
                                          String environment, List<String> provides,
                                          Map<String, String> depends,
                                          Map<String, String> recommends,
                                          Map<String, String> suggests,
                                          Map<String, String> conflicts,
                                          Map<String, String> breaks) {
        invokeOptionalLoaderStatic("registerMod",
            new Class<?>[] {String.class, String.class, String.class, Path.class,
                            String.class, List.class, Map.class, Map.class, Map.class,
                            Map.class, Map.class},
            id, name, version, root, environment, provides, depends, recommends,
            suggests, conflicts, breaks);
    }

    private static boolean matchesLoaderVersion(String actual, String requirement) {
        Object result = invokeOptionalLoaderStatic("matchesVersion",
            new Class<?>[] {String.class, String.class}, actual, requirement);
        // A missing helper means an official Loader is in charge of dependency
        // resolution; fail closed if this compatibility facade is asked to
        // validate a candidate anyway.
        return result instanceof Boolean && (Boolean) result;
    }

    private static <T> List<T> snapshot(List<T> list) {
        return List.copyOf(list);
    }

    private static void closeModLoader() {
        ClassLoader loader = modLoader;
        modLoader = null;
        if (loader instanceof URLClassLoader urls && loader != CppModRuntime.class.getClassLoader()) {
            try { urls.close(); }
            catch (Throwable failure) { log("WARN", "mod classloader close failed: " + failure); }
        }
        ThreadContextClassLoaderLease contextLease = modContextClassLoaderLease;
        modContextClassLoaderLease = null;
        if (contextLease != null) {
            try { contextLease.close(); }
            catch (Throwable failure) { log("WARN", "context classloader restore failed: " + failure); }
        }
        NestedJarSupport.Expansion expansion = modExpansion;
        modExpansion = null;
        if (expansion != null) {
            try { expansion.close(); }
            catch (Throwable failure) { log("WARN", "nested mod expansion close failed: " + failure); }
        }
    }

    private static void invokeSafely(Runnable action, String operation) {
        try { action.run(); } catch (Throwable failure) { log("ERROR", operation + " failed: " + failure); }
    }

    private static void invokeOptionalEvent(String ownerName, String fieldName,
                                            String callbackName, Class<?>[] parameterTypes,
                                            String operation, Object... arguments) {
        try {
            Class<?> owner = Class.forName(ownerName, true, CppModRuntime.class.getClassLoader());
            java.lang.reflect.Field field = owner.getField(fieldName);
            if (!java.lang.reflect.Modifier.isStatic(field.getModifiers())) return;
            if (!(field.get(null) instanceof Event<?> event)) return;
            java.lang.reflect.Type genericType = field.getGenericType();
            if (!(genericType instanceof java.lang.reflect.ParameterizedType parameterized)) return;
            java.lang.reflect.Type listenerType = parameterized.getActualTypeArguments()[0];
            if (!(listenerType instanceof Class<?> listenerClass)) return;
            Method callback = listenerClass.getMethod(callbackName, parameterTypes);
            callback.invoke(event.invoker(), arguments);
        } catch (ClassNotFoundException | NoSuchFieldException missingOptionalApi) {
            // This is a cppfm-only compatibility extension, not a Fabric API guarantee.
        } catch (InvocationTargetException failure) {
            log("ERROR", operation + " failed: " + failure.getCause());
        } catch (ReflectiveOperationException | LinkageError | RuntimeException failure) {
            log("ERROR", operation + " dispatch failed: " + failure);
        }
    }

    @FunctionalInterface
    private interface BooleanAction { boolean run() throws Throwable; }

    private static boolean invokeAllow(BooleanAction action, String operation) {
        try { return action.run(); }
        catch (Throwable failure) {
            // A faulty optional callback cannot accidentally cancel native
            // gameplay, and the exception is consumed before JNI returns.
            log("ERROR", operation + " failed: " + failure);
            return true;
        }
    }

    @FunctionalInterface
    private interface ResultAction<T> { T run() throws Throwable; }

    private static <T> T invokeResult(ResultAction<T> action, String operation) {
        try { return action.run(); }
        catch (Throwable failure) {
            log("ERROR", operation + " failed: " + failure);
            return null;
        }
    }

    private static void log(String level, Object message) {
        try { NativeBridge.nativeLog(level, String.valueOf(message)); }
        catch (Throwable ignored) { System.err.println("[cppfm][jvm][" + level + "] " + message); }
    }

    private record ServerReloadRegistration(
            Identifier id,
            IdentifiableResourceReloadListener listener,
            Function<RegistryWrapper.WrapperLookup, IdentifiableResourceReloadListener> factory) { }

    private record ResolvedReloadListener(
            Identifier id,
            IdentifiableResourceReloadListener listener,
            List<Identifier> dependencies) { }

    private record ReloadPlan(List<ResolvedReloadListener> listeners, boolean success) { }

}
