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
import java.util.Deque;
import java.util.List;
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
    private final Deque<Runnable> tasks = new ArrayDeque<>();
    private static final int MAX_TASKS_PER_TICK = 10_000;
    private volatile boolean stopping;
    private volatile long tick;
    private final CommandManager commandManager;
    private final PlayerManager playerManager;
    private final GameRules gameRules = new GameRules();
    private final net.minecraft.network.message.MessageDecorator messageDecorator =
        net.minecraft.network.message.MessageDecorator.NOOP;

    protected MinecraftServer(long nativeHandle) {
        this.nativeHandle = nativeHandle;
        this.commandManager = new CommandManager(
            CommandManager.RegistrationEnvironment.DEDICATED,
            new net.minecraft.command.CommandRegistryAccess());
        this.playerManager = new PlayerManager();
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
        return ServerWorld.of(NativeAccess.serverWorld(0), this);
    }
    public ServerWorld getWorld(RegistryKey<World> key) {
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
        if (task == null || stopping) return;
        if (Thread.currentThread() == thread) {
            task.run();
            return;
        }
        synchronized (taskLock) {
            if (!stopping) tasks.addLast(task);
        }
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
    public java.util.concurrent.CompletableFuture<Void> reloadResources(
            java.util.Collection<?> resourcePacks) {
        boolean success = CppModRuntime.onDataPackReload();
        return success
            ? java.util.concurrent.CompletableFuture.completedFuture(null)
            : java.util.concurrent.CompletableFuture.failedFuture(
                new IllegalStateException("server resource reload had listener errors; see log"));
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
            Runnable task;
            synchronized (taskLock) {
                task = tasks.pollFirst();
            }
            if (task == null) return;
            ++drained;
            try {
                task.run();
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
        ServerWorld overworld = getOverworld();
        ServerWorld nether = ServerWorld.of(NativeAccess.serverWorld(-1), this);
        ServerWorld end = ServerWorld.of(NativeAccess.serverWorld(1), this);
        return List.of(overworld, nether, end);
    }
    public GameRules getGameRules() { return gameRules; }
    public ServerCommandSource getCommandSource() { return new ServerCommandSource(null, this); }
    public boolean isRunning() { return nativeHandle != 0 && !stopping; }
    public boolean isStopping() { return stopping; }
    public void executeSync(Runnable task) { execute(task); }
    public String getVersion() { return "1.21.4"; }
    public String getServerModName() { return "cpp-fabricmc"; }

    /** Adopt the native tick thread as the owner of the server executor. */
    public void adoptCurrentThread() {
        if (!stopping) thread = Thread.currentThread();
    }

    /** Reject new work and discard work that has not crossed the tick boundary. */
    public void beginShutdown() {
        stopping = true;
        synchronized (taskLock) { tasks.clear(); }
    }
    /** Yarn's mapped entrypoint delegates to the metadata builder. */
    public ServerMetadata.Players buildPlayerStatus() { return createMetadataPlayers(); }
    /** Mojang-mapped body; Carpet's constant injector targets this method. */
    public ServerMetadata.Players createMetadataPlayers() {
        return new ServerMetadata.Players(12, getPlayerManager().getCurrentPlayerCount(), List.of());
    }
}
