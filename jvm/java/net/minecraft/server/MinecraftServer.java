package net.minecraft.server;

import cppfm.bridge.WrapperCache;
import cppfm.bridge.MixinHooks;
import cppfm.bridge.CppModRuntime;
import net.minecraft.registry.RegistryKey;
import net.minecraft.text.Text;
import net.minecraft.world.World;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.server.command.CommandManager;
import net.minecraft.server.command.ServerCommandSource;
import net.minecraft.util.NativeAccess;
import net.minecraft.world.GameRules;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Deque;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.function.Consumer;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

public class MinecraftServer {
    private final long nativeHandle;
    // The wrapper is constructed before the native tick loop starts.  Keep
    // this mutable so the first authoritative server tick can adopt its
    // thread; otherwise execute() would incorrectly treat the bootstrap
    // thread as the server thread forever.
    private volatile Thread thread = Thread.currentThread();
    private final Object taskLock = new Object();
    private final Deque<ServerTask> tasks = new ArrayDeque<>();
    private final List<Consumer<RuntimeException>> stopCallbacks = new ArrayList<>();
    private static final int MAX_TASKS_PER_TICK = 10_000;
    private volatile boolean stopping;
    private volatile boolean worldsAvailable;
    private volatile long tick;
    private final CommandManager commandManager;
    private volatile PlayerManager playerManager;
    private final GameRules gameRules = new GameRules();
    private final net.minecraft.network.message.MessageDecorator messageDecorator =
        net.minecraft.network.message.MessageDecorator.NOOP;

    private static final class ServerTask {
        private final Runnable action;
        private final Consumer<RuntimeException> discarded;

        private ServerTask(Runnable action, Consumer<RuntimeException> discarded) {
            this.action = action;
            this.discarded = discarded;
        }

        private void discard(RuntimeException reason) {
            if (discarded == null) return;
            try {
                discarded.accept(reason);
            } catch (Throwable failure) {
                NativeAccess.log("ERROR", "server task discard handler failed: " + failure);
            }
        }
    }

    protected MinecraftServer(long nativeHandle) {
        this.nativeHandle = nativeHandle;
        this.commandManager = new CommandManager(
            CommandManager.RegistrationEnvironment.DEDICATED,
            new net.minecraft.command.CommandRegistryAccess());
    }

    /**
     * 1.21.4 Mojang/Fabric constructor shape.  The native server owns the
     * actual loader and storage objects; retaining the complete JVM
     * signature lets lifecycle mixins link against the same target while the
     * wrapper remains deliberately lightweight.
     */
    protected MinecraftServer(Thread serverThread,
                               net.minecraft.world.level.storage.LevelStorage.Session session,
                               net.minecraft.resource.ResourcePackManager resourcePackManager,
                               SaveLoader saveLoader,
                               java.net.Proxy proxy,
                               com.mojang.datafixers.DataFixer dataFixer,
                               net.minecraft.util.ApiServices apiServices,
                               WorldGenerationProgressListenerFactory progressListenerFactory) {
        this(0L);
    }
    public static MinecraftServer of(long handle) {
        return handle == 0L
            ? WrapperCache.getAllowZero(MinecraftServer.class, MinecraftServer::new)
            : WrapperCache.get(MinecraftServer.class, handle, MinecraftServer::new);
    }
    public long nativeHandle() { return nativeHandle; }
    public Thread getThread() { return thread; }
    public int getTicks() {
        Integer overwritten = MixinHooks.invokeOverwrite(this, "getTicks");
        if (overwritten != null) return overwritten;
        CallbackInfoReturnable<Integer> tail = MixinHooks.invokeTailReturn(
            this, "getTicks", (int) tick);
        if (tail.isCancelled()) return tail.getReturnValue();
        CallbackInfoReturnable<Integer> returned = MixinHooks.invokeReturn(
            this, "getTicks", tail.getReturnValue());
        return returned.getReturnValue();
    }
    public long getTickTime() {
        Long overwritten = MixinHooks.invokeOverwrite(this, "getTickTime");
        return overwritten != null ? overwritten : tick;
    }
    public void setTick(long tick) {
        // Void overwrites cannot be distinguished from a handler that returns
        // null through this bounded API, so the explicit HEAD hook remains
        // the safe path for setter methods.
        CallbackInfo callback = MixinHooks.invokeHead(this, "setTick", tick);
        if (callback.isCancelled()) return;
        this.tick = tick;
        MixinHooks.invokeTail(this, "setTick", tick);
    }
    public ServerWorld getOverworld() {
        return worldsAvailable
            ? ServerWorld.of(NativeAccess.serverWorld(0), this) : null;
    }
    public ServerWorld getWorld(RegistryKey<World> key) {
        if (!worldsAvailable) return null;
        String value = key == null ? "minecraft:overworld" : key.getValue().toString();
        int dimension = value.endsWith("the_nether") ? -1 : value.endsWith("the_end") ? 1 : 0;
        return ServerWorld.of(NativeAccess.serverWorld(dimension), this);
    }
    public void sendSystemMessage(Text text) {
        if (text != null) NativeAccess.log("INFO", text.getString());
    }
    /**
     * Submit work to the server executor.  Vanilla runs work submitted from a
     * foreign thread on the server thread; executing it inline here allowed a
     * Java worker to mutate native game state concurrently with the tick.
     */
    public void execute(Runnable task) {
        submitTask(task, null);
    }

    /** Queue work for the server owner and report whether it was accepted. */
    private boolean submitTask(Runnable action, Consumer<RuntimeException> discarded) {
        if (action == null || stopping) return false;
        if (Thread.currentThread() == thread) {
            if (stopping) return false;
            action.run();
            return true;
        }
        synchronized (taskLock) {
            if (stopping) return false;
            tasks.addLast(new ServerTask(action, discarded));
            return true;
        }
    }

    /** Register a one-shot observer for stop; the returned action unregisters it. */
    public Runnable addStopCallback(Consumer<RuntimeException> callback) {
        if (callback == null) throw new NullPointerException("callback");
        RuntimeException alreadyStopped = null;
        synchronized (taskLock) {
            if (stopping) alreadyStopped = new IllegalStateException("server is stopping");
            else stopCallbacks.add(callback);
        }
        if (alreadyStopped != null) callback.accept(alreadyStopped);
        return () -> {
            synchronized (taskLock) { stopCallbacks.remove(callback); }
        };
    }

    /** Queue reload work on the owner thread and report rejection/discard. */
    public boolean executeResourceReloadTask(Runnable action,
                                             Consumer<RuntimeException> rejected) {
        if (action == null) throw new NullPointerException("action");
        if (rejected == null) throw new NullPointerException("rejected");
        boolean accepted = submitTask(action, rejected);
        if (!accepted) rejected.accept(new RejectedExecutionException(
            "server rejected resource reload work because it is stopping"));
        return accepted;
    }

    /** Executor variant used by reloaders for game-thread apply work. */
    public Executor resourceReloadExecutor(Consumer<RuntimeException> rejected) {
        if (rejected == null) throw new NullPointerException("rejected");
        return action -> {
            if (!executeResourceReloadTask(action, rejected))
                throw new RejectedExecutionException(
                    "server rejected resource reload work because it is stopping");
        };
    }
    /** Main server tick ABI used by Mojang-mapped mixins. */
    public void tick(java.util.function.BooleanSupplier shouldKeepTicking) {
        if (shouldKeepTicking == null || shouldKeepTicking.getAsBoolean()) tick++;
    }
    /** Lifecycle hook exposed by the 1.21.4 server mixin surface. */
    public void loadWorld() { }
    /** World bootstrap hook used by Fabric lifecycle and profiling mixins. */
    public void createWorlds(WorldGenerationProgressListener progressListener) { }
    /** Resource-reload hook used by Fabric's server lifecycle events. */
    public CompletableFuture<Void> reloadResources(Collection<String> dataPacks) {
        if (dataPacks == null) return CompletableFuture.failedFuture(
            new IllegalArgumentException("data-pack selection must not be null"));
        final List<String> selectedDataPacks;
        try {
            selectedDataPacks = List.copyOf(dataPacks);
        } catch (RuntimeException invalidSelection) {
            return CompletableFuture.failedFuture(new IllegalArgumentException(
                "data-pack selection contains an invalid entry", invalidSelection));
        }
        CompletableFuture<Void> result = new CompletableFuture<>();
        RuntimeException stopped = new IllegalStateException(
            "server stopped before resource reload could run");
        Runnable unregisterStop = addStopCallback(result::completeExceptionally);
        result.whenComplete((ignored, failure) -> unregisterStop.run());
        boolean accepted = submitTask(() -> {
            if (stopping) {
                result.completeExceptionally(stopped);
                return;
            }
            CompletableFuture<Void> reload;
            try {
                reload = cppfm.bridge.KnotLauncher.isKnotProviderSelected()
                    ? cppfm.bridge.KnotLauncher.onDataPackReload(selectedDataPacks)
                    : CppModRuntime.onDataPackReload(selectedDataPacks);
            } catch (Throwable failure) {
                result.completeExceptionally(failure);
                return;
            }
            if (reload == null) {
                result.completeExceptionally(new IllegalStateException(
                    "resource reload provider returned a null future"));
                return;
            }
            reload.whenComplete((ignored, failure) -> {
                if (failure == null) result.complete(null);
                else result.completeExceptionally(unwrapReloadFailure(failure));
            });
        }, result::completeExceptionally);
        if (!accepted) result.completeExceptionally(stopped);
        return result;
    }

    private static Throwable unwrapReloadFailure(Throwable failure) {
        Throwable cause = failure;
        while ((cause instanceof java.util.concurrent.CompletionException ||
                cause instanceof java.util.concurrent.ExecutionException) &&
                cause.getCause() != null) cause = cause.getCause();
        return cause;
    }
    /** Data-pack selection hook used by the server lifecycle mixin. */
    public net.minecraft.resource.DataConfiguration loadDataPacks(
            net.minecraft.resource.ResourcePackManager resourcePackManager,
            net.minecraft.resource.DataConfiguration dataConfiguration,
            boolean safeMode, boolean initMode) {
        return dataConfiguration == null ? new net.minecraft.resource.DataConfiguration() : dataConfiguration;
    }
    /** Shutdown lifecycle hook exposed by server mixins. */
    public void shutdown() { beginShutdown(); }
    /** Main server loop entrypoint used by Carpet's tick-speed mixin. */
    public void runServer() {
        net.minecraft.util.profiling.Profiler.get();
    }
    /** Autosave entrypoint used by Carpet's server-loop instrumentation. */
    public void runAutosave() {
        saveAll(false, false, false);
    }
    /** Default autosave cadence exposed by the vanilla server ABI. */
    public int getAutosaveInterval() { return 6000; }
    public boolean saveAll(boolean flush, boolean suppressLogs, boolean force) { return true; }
    /** Intermediary/Yarn save symbol used by Fabric's lifecycle mixin. */
    public boolean save(boolean flush, boolean suppressLogs, boolean force) {
        return saveAll(flush, suppressLogs, force);
    }
    /** Mojang-mapped alias retained for mixin targets compiled outside Yarn. */
    public boolean saveEverything(boolean flush, boolean suppressLogs, boolean force) {
        return saveAll(flush, suppressLogs, force);
    }
    /** Network service tick hook used by Carpet's profiler mixin. */
    public void tickNetworkIo() { }
    /** End-of-tick task drain hook used by the 1.21.4 server loop. */
    public void runTasksTillTickEnd() {
        if (Thread.currentThread() != thread) return;
        int drained = 0;
        while (drained < MAX_TASKS_PER_TICK) {
            ServerTask task;
            synchronized (taskLock) {
                task = tasks.pollFirst();
            }
            if (task == null) return;
            ++drained;
            if (stopping) {
                task.discard(new IllegalStateException(
                    "server stopped before queued task could run"));
                continue;
            }
            try {
                task.action.run();
            } catch (Throwable failure) {
                NativeAccess.log("ERROR", "server task failed: " + failure);
            }
        }
        synchronized (taskLock) {
            if (!tasks.isEmpty()) {
                NativeAccess.log("WARN", "server task queue exceeded per-tick budget; deferring "
                    + tasks.size() + " task(s)");
            }
        }
    }
    public boolean isDedicated() { return true; }
    /** Current server-side chat decoration strategy. */
    public net.minecraft.network.message.MessageDecorator getMessageDecorator() {
        return messageDecorator;
    }
    public CommandManager getCommandManager() { return commandManager; }
    public PlayerManager getPlayerManager() { return playerManager; }
    public List<ServerWorld> getWorlds() {
        if (!worldsAvailable) return List.of();
        ServerWorld overworld = getOverworld();
        ServerWorld nether = ServerWorld.of(NativeAccess.serverWorld(-1), this);
        ServerWorld end = ServerWorld.of(NativeAccess.serverWorld(1), this);
        return List.of(overworld, nether, end);
    }
    public GameRules getGameRules() { return gameRules; }
    public ServerCommandSource getCommandSource() { return new ServerCommandSource(null, this); }
    public boolean isRunning() { return nativeHandle != 0 && !stopping; }
    public boolean isStopping() { return stopping; }
    public boolean isOnThread() { return Thread.currentThread() == thread; }
    public void executeSync(Runnable task) { execute(task); }
    public String getVersion() { return "1.21.4"; }
    public String getServerModName() { return "cpp-fabricmc"; }

    /** Adopt the native tick thread as the owner of the server executor. */
    public void adoptCurrentThread() {
        if (!stopping) thread = Thread.currentThread();
    }

    /** Reject new work and discard work that has not crossed the tick boundary. */
    public void beginShutdown() {
        List<ServerTask> abandoned;
        List<Consumer<RuntimeException>> callbacks;
        synchronized (taskLock) {
            if (stopping) return;
            stopping = true;
            abandoned = new ArrayList<>(tasks);
            tasks.clear();
            callbacks = new ArrayList<>(stopCallbacks);
            stopCallbacks.clear();
        }
        RuntimeException reason = new IllegalStateException(
            "server stopped before queued task could run");
        for (Consumer<RuntimeException> callback : callbacks) {
            try { callback.accept(reason); }
            catch (Throwable failure) {
                NativeAccess.log("ERROR", "server stop callback failed: " + failure);
            }
        }
        for (ServerTask task : abandoned) task.discard(reason);
    }
    /** Called after Fabric's per-world unload callbacks have returned. */
    public void finishWorldUnload() {
        worldsAvailable = false;
        playerManager = null;
    }
    public void finishWorldLoad() {
        playerManager = new PlayerManager();
        worldsAvailable = true;
    }
    /** Yarn's mapped entrypoint delegates to the metadata builder. */
    public ServerMetadata.Players buildPlayerStatus() { return createMetadataPlayers(); }
    /** Mojang-mapped body; Carpet's constant injector targets this method. */
    public ServerMetadata.Players createMetadataPlayers() {
        PlayerManager currentPlayerManager = playerManager;
        int currentPlayers = currentPlayerManager == null
            ? 0 : currentPlayerManager.getCurrentPlayerCount();
        return new ServerMetadata.Players(12, currentPlayers, List.of());
    }
}
