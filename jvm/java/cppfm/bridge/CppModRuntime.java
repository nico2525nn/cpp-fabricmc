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
import java.util.concurrent.TimeUnit;
import java.util.function.Function;
import java.util.stream.Stream;
import java.util.jar.JarFile;

import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.api.ModInitializer;
import net.fabricmc.fabric.api.command.v2.CommandRegistrationCallback;
import net.fabricmc.fabric.api.entity.event.v1.ServerEntityEvents;
import net.fabricmc.fabric.api.entity.event.v1.ServerPlayerEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerWorldEvents;
import net.fabricmc.fabric.api.event.player.AttackBlockCallback;
import net.fabricmc.fabric.api.event.player.PlayerBlockBreakEvents;
import net.fabricmc.fabric.api.event.player.UseBlockCallback;
import net.fabricmc.fabric.api.event.player.UseEntityCallback;
import net.fabricmc.fabric.api.event.player.UseItemCallback;
import net.fabricmc.fabric.api.message.v1.ServerMessageEvents;
import net.fabricmc.fabric.api.networking.v1.ServerPlayConnectionEvents;
import net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking;
import net.fabricmc.fabric.api.resource.IdentifiableResourceReloadListener;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.block.BlockState;
import net.minecraft.entity.DamageSource;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.network.message.MessageType;
import net.minecraft.network.message.SignedMessage;
import net.minecraft.resource.LifecycledResourceManager;
import net.minecraft.resource.ResourceReloader;
import net.minecraft.resource.ResourceType;
import net.minecraft.registry.RegistryWrapper;
import net.minecraft.server.MinecraftServer;
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
import net.minecraft.util.profiler.Profiler;

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
    private static final List<ServerLifecycleEvents.ServerStarted> SERVER_STARTED = new CopyOnWriteArrayList<>();
    private static final List<ServerLifecycleEvents.ServerStarting> SERVER_STARTING = new CopyOnWriteArrayList<>();
    private static final List<ServerLifecycleEvents.ServerStopping> SERVER_STOPPING = new CopyOnWriteArrayList<>();
    private static final List<ServerLifecycleEvents.ServerStopped> SERVER_STOPPED = new CopyOnWriteArrayList<>();
    private static final List<ServerLifecycleEvents.SyncDataPackContents> SYNC_DATA_PACK_CONTENTS = new CopyOnWriteArrayList<>();
    private static final List<ServerLifecycleEvents.StartDataPackReload> START_DATA_PACK_RELOAD = new CopyOnWriteArrayList<>();
    private static final List<ServerLifecycleEvents.EndDataPackReload> END_DATA_PACK_RELOAD = new CopyOnWriteArrayList<>();
    private static final List<ServerReloadRegistration> SERVER_RELOAD_LISTENERS = new CopyOnWriteArrayList<>();
    private static final Object SERVER_RELOAD_LOCK = new Object();
    private static final List<ServerLifecycleEvents.BeforeSave> BEFORE_SAVE = new CopyOnWriteArrayList<>();
    private static final List<ServerLifecycleEvents.AfterSave> AFTER_SAVE = new CopyOnWriteArrayList<>();
    private static final List<ServerTickEvents.Start> TICK_START = new CopyOnWriteArrayList<>();
    private static final List<ServerTickEvents.End> TICK_END = new CopyOnWriteArrayList<>();
    private static final List<ServerTickEvents.StartServerTick> SERVER_TICK_START = new CopyOnWriteArrayList<>();
    private static final List<ServerTickEvents.EndServerTick> SERVER_TICK_END = new CopyOnWriteArrayList<>();
    private static final List<ServerTickEvents.StartWorldTick> WORLD_TICK_START = new CopyOnWriteArrayList<>();
    private static final List<ServerTickEvents.EndWorldTick> WORLD_TICK_END = new CopyOnWriteArrayList<>();
    private static final List<ServerWorldEvents.Load> WORLD_LOAD = new CopyOnWriteArrayList<>();
    private static final List<ServerWorldEvents.Unload> WORLD_UNLOAD = new CopyOnWriteArrayList<>();
    private static final List<ServerPlayConnectionEvents.Join> PLAYER_JOIN = new CopyOnWriteArrayList<>();
    private static final List<ServerPlayConnectionEvents.Disconnect> PLAYER_QUIT = new CopyOnWriteArrayList<>();
    private static final List<ServerPlayerEvents.CopyFrom> PLAYER_COPY_FROM = new CopyOnWriteArrayList<>();
    private static final List<ServerPlayerEvents.AfterRespawn> PLAYER_AFTER_RESPAWN = new CopyOnWriteArrayList<>();
    private static final List<ServerPlayerEvents.Join> PLAYER_JOIN_EVENT = new CopyOnWriteArrayList<>();
    private static final List<ServerPlayerEvents.Leave> PLAYER_LEAVE_EVENT = new CopyOnWriteArrayList<>();
    private static final List<ServerEntityEvents.Load> ENTITY_LOAD = new CopyOnWriteArrayList<>();
    private static final List<ServerEntityEvents.Unload> ENTITY_UNLOAD = new CopyOnWriteArrayList<>();
    private static final List<net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AllowDamage> ALLOW_DAMAGE = new CopyOnWriteArrayList<>();
    private static final List<net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AfterDamage> AFTER_DAMAGE = new CopyOnWriteArrayList<>();
    private static final List<net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AfterDeath> AFTER_DEATH = new CopyOnWriteArrayList<>();
    private static final List<net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents.Load> LEGACY_ENTITY_LOAD = new CopyOnWriteArrayList<>();
    private static final List<net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents.Unload> LEGACY_ENTITY_UNLOAD = new CopyOnWriteArrayList<>();
    private static final List<net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AllowDamage> LEGACY_ALLOW_DAMAGE = new CopyOnWriteArrayList<>();
    private static final List<net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AfterDamage> LEGACY_AFTER_DAMAGE = new CopyOnWriteArrayList<>();
    private static final List<net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AfterDeath> LEGACY_AFTER_DEATH = new CopyOnWriteArrayList<>();
    private static final List<ServerMessageEvents.AllowChatMessage> ALLOW_CHAT_MESSAGE = new CopyOnWriteArrayList<>();
    private static final List<ServerMessageEvents.ChatMessage> CHAT_MESSAGE = new CopyOnWriteArrayList<>();
    private static final List<UseBlockCallback> USE_BLOCK = new CopyOnWriteArrayList<>();
    private static final List<AttackBlockCallback> ATTACK_BLOCK = new CopyOnWriteArrayList<>();
    private static final List<PlayerBlockBreakEvents.Before> BEFORE_BREAK = new CopyOnWriteArrayList<>();
    private static final List<CommandRegistrationCallback> COMMAND_REGISTRATION = new CopyOnWriteArrayList<>();
    private static final Map<String, String> savedLoaderProperties = new LinkedHashMap<>();
    private static ClassLoader modLoader;
    private static ClassLoader previousContextClassLoader;
    private static NestedJarSupport.Expansion modExpansion;
    private static MinecraftServer server;
    private static boolean bootstrapped;
    // These ids are supplied by the game/loader environment rather than by a
    // jar under the configured mods directory.  Treating them as ordinary
    // candidates makes otherwise valid Fabric metadata fail before an
    // entrypoint can be initialized (for example `minecraft` and
    // `fabricloader` in the public Carpet/FerriteCore jars).
    private static final Set<String> BUILTIN_DEPENDENCIES =
        Set.of("fabricloader", "minecraft", "java");
    private static final Map<String, String> BUILTIN_VERSIONS = Map.of(
        "fabricloader", "0.16.9",
        "minecraft", "1.21.4",
        "java", javaMajorVersion());

    private CppModRuntime() {}

    public static synchronized boolean bootstrap(String modsDir, String configDir) {
        if (bootstrapped) return true;
        try {
            loadMods(Paths.get(modsDir));
            if (server == null) server = MinecraftServer.of(NativeAccess.serverHandle());
            bootstrapped = true;
            for (ServerLifecycleEvents.ServerStarting callback : snapshot(SERVER_STARTING))
                invokeSafely(() -> callback.onServerStarting(server), "server starting");
            fireWorldLoad();
            for (ServerLifecycleEvents.ServerStarted callback : snapshot(SERVER_STARTED))
                invokeSafely(() -> callback.onServerStarted(server), "server started");
            return true;
        } catch (Throwable failure) {
            Throwable cause = failure;
            while ((cause instanceof InvocationTargetException
                    || cause instanceof ExceptionInInitializerError)
                   && cause.getCause() != null)
                cause = cause.getCause();
            StringWriter details = new StringWriter();
            cause.printStackTrace(new PrintWriter(details));
            log("ERROR", "mod bootstrap failed:\n" + details);
            if (Boolean.getBoolean("cppfm.jvm.strict")) {
                closeModLoader();
                return false;
            }
            // A malformed optional mod must not make the native server vanish.
            bootstrapped = true;
            return true;
        }
    }

    public static synchronized void shutdown() {
        if (server != null) server.beginShutdown();
        if (!bootstrapped) return;
        fireWorldUnload();
        for (ServerLifecycleEvents.ServerStopping callback : snapshot(SERVER_STOPPING))
            invokeSafely(() -> callback.onServerStopping(server), "server stopping");
        for (ServerLifecycleEvents.ServerStopped callback : snapshot(SERVER_STOPPED))
            invokeSafely(() -> callback.onServerStopped(server), "server stopped");
        WrapperCache.clear();
        clearRegistrations();
        MixinHooks.clear();
        ModEvents.clearAll();
        clearLoaderRuntime();
        closeModLoader();
        bootstrapped = false;
        server = null;
    }

    public static synchronized void onServerTick(long tick) {
        if (!bootstrapped) return;
        if (server != null) server.adoptCurrentThread();
        // JvmRuntime dispatches a transformed setTick(J)V body before this
        // facade.  Do not call it a second time: a second call would execute
        // every injected handler twice.  With no transformed route this is
        // the historical shadow/native path and remains unchanged; the
        // NativeAccess fallback returns zero for javac-only execution.
        if (server != null && NativeAccess.routePath(
                "net/minecraft/server/MinecraftServer", "setTick", "(J)V") == 0)
            server.setTick(tick);
        for (ModEvents.Tick callback : ModEvents.TICK.snapshot())
            invokeSafely(() -> callback.onTick(server, tick), "cppfm tick");
        for (ServerTickEvents.Start callback : snapshot(TICK_START))
            invokeSafely(() -> callback.onStartTick(server), "server tick start");
        for (ServerTickEvents.End callback : snapshot(TICK_END))
            invokeSafely(() -> callback.onEndTick(server), "server tick end");
        for (ServerTickEvents.StartServerTick callback : snapshot(SERVER_TICK_START))
            invokeSafely(() -> callback.onStartTick(server), "server tick start (fabric)");
        if (server != null) {
            for (ServerWorld world : server.getWorlds()) {
                for (ServerTickEvents.StartWorldTick callback : snapshot(WORLD_TICK_START))
                    invokeSafely(() -> callback.onStartTick((ServerWorld) world), "world tick start");
            }
        }
        if (server != null) {
            for (ServerWorld world : server.getWorlds()) {
                for (ServerTickEvents.EndWorldTick callback : snapshot(WORLD_TICK_END))
                    invokeSafely(() -> callback.onEndTick(world), "world tick end");
            }
        }
        for (ServerTickEvents.EndServerTick callback : snapshot(SERVER_TICK_END))
            invokeSafely(() -> callback.onEndTick(server), "server tick end (fabric)");
        if (server != null) server.runTasksTillTickEnd();
    }

    public static void onPlayerJoin(long handle) {
        if (!bootstrapped) return;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerPlayNetworkHandler network = player.getNetworkHandler();
        for (ServerPlayConnectionEvents.Join callback : snapshot(PLAYER_JOIN))
            invokeSafely(() -> callback.onPlayReady(network, ServerPlayNetworking.getSender(player), server), "player join");
        for (ServerPlayerEvents.Join callback : snapshot(PLAYER_JOIN_EVENT))
            invokeSafely(() -> callback.onPlayReady(network, player), "player entity join");
        for (ServerLifecycleEvents.SyncDataPackContents callback : snapshot(SYNC_DATA_PACK_CONTENTS))
            invokeSafely(() -> callback.onSyncDataPackContents(player, true), "data-pack sync");
        dispatchEntityLoad(player, player.getServerWorld());
    }

    public static void onPlayerQuit(long handle) {
        if (!bootstrapped) return;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerPlayNetworkHandler network = player.getNetworkHandler();
        for (ServerPlayerEvents.Leave callback : snapshot(PLAYER_LEAVE_EVENT))
            invokeSafely(() -> callback.onPlayDisconnect(network, player), "player entity leave");
        for (ServerPlayConnectionEvents.Disconnect callback : snapshot(PLAYER_QUIT))
            invokeSafely(() -> callback.onPlayDisconnect(network, server), "player quit");
        dispatchEntityUnload(player, player.getServerWorld());
        WrapperCache.remove(handle);
    }

    /** Dispatches the Fabric data-pack reload-start lifecycle hook. */
    public static void onDataPackReloadStart(LifecycledResourceManager resourceManager) {
        if (!bootstrapped) return;
        for (ServerLifecycleEvents.StartDataPackReload callback : snapshot(START_DATA_PACK_RELOAD))
            invokeSafely(() -> callback.startDataPackReload(server, resourceManager),
                         "data-pack reload start");
    }

    /** Dispatches the Fabric data-pack reload-end lifecycle hook. */
    public static void onDataPackReloadEnd(LifecycledResourceManager resourceManager,
                                           boolean success) {
        if (!bootstrapped) return;
        for (ServerLifecycleEvents.EndDataPackReload callback : snapshot(END_DATA_PACK_RELOAD))
            invokeSafely(() -> callback.endDataPackReload(server, resourceManager, success),
                         "data-pack reload end");
    }

    /**
     * Runs the bounded server-data resource reload bridge used by native
     * {@code /reload}.  Listener failures are isolated per listener and are
     * returned as a failed result rather than escaping through JNI.
     */
    public static boolean onDataPackReload() {
        if (!bootstrapped) return false;
        synchronized (SERVER_RELOAD_LOCK) {
            LifecycledResourceManager resourceManager = createServerResourceManager();
            boolean success = true;
            ReloadPlan plan = resolveServerReloadListeners();
            log("INFO", "server resource reload start listeners=" + plan.listeners.size());
            try {
                onDataPackReloadStart(resourceManager);
                ReloadPlan ordered = orderServerReloadListeners(plan.listeners);
                if (!plan.success || !ordered.success) {
                    success = false;
                } else {
                    ResourceReloader.Synchronizer synchronizer = new ResourceReloader.Synchronizer() {
                        @Override
                        public <T> CompletableFuture<T> whenPrepared(T preparedObject) {
                            return CompletableFuture.completedFuture(preparedObject);
                        }
                    };
                    java.util.concurrent.Executor directExecutor = Runnable::run;
                    for (ResolvedReloadListener listener : ordered.listeners) {
                        try {
                            CompletableFuture<Void> future = listener.listener.reload(
                                synchronizer, resourceManager, Profiler.get(), Profiler.get(),
                                directExecutor, directExecutor);
                            if (future == null)
                                throw new IllegalStateException("listener returned null future");
                            future.orTimeout(30, TimeUnit.SECONDS).join();
                            log("INFO", "server resource reload listener " + listener.id
                                + " completed");
                        } catch (Throwable failure) {
                            success = false;
                            logReloadFailure("server resource reload listener " + listener.id,
                                              failure);
                        }
                    }
                }
            } catch (Throwable failure) {
                success = false;
                logReloadFailure("server resource reload", failure);
            } finally {
                onDataPackReloadEnd(resourceManager, success);
                try {
                    resourceManager.close();
                } catch (Throwable failure) {
                    success = false;
                    logReloadFailure("server resource reload resource manager close", failure);
                }
            }
            log("INFO", "server resource reload complete success=" + success
                + " listeners=" + plan.listeners.size());
            return success;
        }
    }

    /** Dispatches the Fabric pre-save lifecycle hook at the native save boundary. */
    public static void onBeforeSave(boolean flush, boolean skipErrors) {
        if (!bootstrapped) return;
        for (ServerLifecycleEvents.BeforeSave callback : snapshot(BEFORE_SAVE))
            invokeSafely(() -> callback.onBeforeSave(server, flush, skipErrors), "before save");
    }

    /** Dispatches the Fabric post-save lifecycle hook at the native save boundary. */
    public static void onAfterSave(boolean flush, boolean skipErrors) {
        if (!bootstrapped) return;
        for (ServerLifecycleEvents.AfterSave callback : snapshot(AFTER_SAVE))
            invokeSafely(() -> callback.onAfterSave(server, flush, skipErrors), "after save");
    }

    /** Return null to cancel, otherwise return the possibly rewritten message. */
    public static String onChat(long handle, String message) {
        if (!bootstrapped) return message;
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
        for (ServerMessageEvents.ChatMessage callback : snapshot(CHAT_MESSAGE))
            invokeSafely(() -> callback.onChatMessage(delivered, player, MessageType.Parameters.EMPTY), "chat message");
        return current;
    }

    public static boolean onBlockBreak(long handle, int x, int y, int z, int rawState) {
        if (!bootstrapped) return true;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerWorld world = player.getServerWorld();
        BlockPos pos = new BlockPos(x, y, z);
        BlockState state = new BlockState(rawState);
        for (PlayerBlockBreakEvents.Before callback : snapshot(BEFORE_BREAK)) {
            if (!callback.beforeBlockBreak(world, player, pos, state, null)) return false;
        }
        for (ModEvents.BlockBreak callback : ModEvents.BLOCK_BREAK.snapshot()) {
            if (!callback.onBlockBreak(player, world, pos, state)) return false;
        }
        return true;
    }

    public static boolean onBlockPlace(long handle, int x, int y, int z, int rawState) {
        if (!bootstrapped) return true;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerWorld world = player.getServerWorld();
        BlockPos pos = new BlockPos(x, y, z);
        BlockState state = new BlockState(rawState);
        for (UseBlockCallback callback : snapshot(USE_BLOCK)) {
            ActionResult result = invokeResult(() -> callback.interact(player, world, Hand.MAIN_HAND,
                                                    new BlockHitResult(pos)), "use block");
            if (result != null && result != ActionResult.PASS) return false;
        }
        for (ModEvents.BlockPlace callback : ModEvents.BLOCK_PLACE.snapshot())
            if (!callback.onBlockPlace(player, world, pos, state)) return false;
        return true;
    }

    public static boolean onBlockClicked(long handle, int x, int y, int z,
                                         int rawState, int face) {
        if (!bootstrapped) return true;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerWorld world = player.getServerWorld();
        BlockPos pos = new BlockPos(x, y, z);
        BlockState state = new BlockState(rawState);
        for (AttackBlockCallback callback : snapshot(ATTACK_BLOCK)) {
            ActionResult result = invokeResult(() -> callback.interact(player, world, Hand.MAIN_HAND,
                                                    pos, net.minecraft.util.math.Direction.byId(face)),
                                                "attack block");
            if (result != null && result != ActionResult.PASS) return false;
        }
        for (ModEvents.BlockClicked callback : ModEvents.BLOCK_CLICKED.snapshot())
            if (!callback.onBlockClicked(player, world, pos, state, face)) return false;
        return true;
    }

    /** Return null to cancel, otherwise return the possibly rewritten command. */
    public static String onCommand(long handle, String command) {
        if (!bootstrapped) return command;
        ServerPlayerEntity player = handle == 0 ? null : ServerPlayerEntity.of(handle);
        String current = command;
        for (ModEvents.Command callback : ModEvents.COMMAND.snapshot()) {
            final String input = current;
            current = invokeResult(() -> callback.onCommand(player, input), "cppfm command");
            if (current == null) return null;
        }
        SignedMessage commandMessage = SignedMessage.of(current);
        if (!invokeAllow(() -> net.fabricmc.fabric.api.message.v1.ServerMessageEvents.ALLOW_COMMAND_MESSAGE.invoker()
                .allowCommandMessage(commandMessage, player, MessageType.Parameters.EMPTY), "allow command message")) return null;
        if ("reload".equals(current.trim())
                && (server == null || !server.getCommandManager().hasCommand(current))) {
            boolean success = onDataPackReload();
            log("INFO", "server /reload Java listeners success=" + success);
        }
        // Commands registered through Fabric's CommandRegistrationCallback
        // must be consumed here, otherwise the native command dispatcher
        // would run a second, unrelated command tree.
        if (server != null && server.getCommandManager().hasCommand(current)) {
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
                player == null ? server.getOverworld() : player.getServerWorld(),
                4,
                player == null ? "Console" : player.getName().getString(),
                player == null ? net.minecraft.text.Text.literal("Console") : player.getName(),
                server,
                player);
            try {
                server.getCommandManager().getDispatcher().execute(current, commandSource);
            } catch (Throwable failure) {
                StringWriter details = new StringWriter();
                failure.printStackTrace(new PrintWriter(details));
                log("ERROR", "command execution failed:\n" + details);
                if (commandResponse.length() == 0) {
                    commandResponse.append("error: ").append(failure.getMessage() == null
                        ? failure.getClass().getSimpleName() : failure.getMessage());
                }
            }
            for (ServerMessageEvents.CommandMessage callback : snapshot(ServerMessageEvents.COMMAND_MESSAGE.snapshot()))
                invokeSafely(() -> callback.onCommandMessage(commandMessage, player, MessageType.Parameters.EMPTY), "command message");
            return HANDLED_COMMAND_PREFIX + commandResponse;
        }
        return current;
    }

    public static boolean onEntityDamage(long playerHandle, long entityHandle,
                                         float amount, String cause) {
        if (!bootstrapped) return true;
        LivingEntity victim = playerHandle != 0 ? ServerPlayerEntity.of(playerHandle) : LivingEntity.of(entityHandle);
        if (victim == null) return true;
        DamageSource source = new DamageSource(cause == null ? "generic" : cause);
        for (net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AllowDamage callback : snapshot(ALLOW_DAMAGE))
            if (!invokeAllow(() -> callback.allowDamage(victim, source, amount), "allow damage")) return false;
        for (net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AllowDamage callback : snapshot(LEGACY_ALLOW_DAMAGE))
            if (!invokeAllow(() -> callback.allowDamage(victim, source, amount), "allow damage (legacy)")) return false;
        for (ModEvents.EntityDamage callback : ModEvents.ENTITY_DAMAGE.snapshot())
            if (!invokeAllow(() -> callback.onEntityDamage(victim, amount, cause), "cppfm damage")) return false;
        return true;
    }

    /**
     * Completion hook for a native damage implementation.  The current native
     * call site only has a pre-damage boolean gate, so AFTER_DAMAGE/AFTER_DEATH
     * are deliberately emitted here or by a future native post-damage hook,
     * never speculatively from onEntityDamage.
     */
    public static void onEntityDamageApplied(long playerHandle, long entityHandle,
                                              float amount, String cause, boolean died) {
        if (!bootstrapped) return;
        LivingEntity victim = playerHandle != 0 ? ServerPlayerEntity.of(playerHandle) : LivingEntity.of(entityHandle);
        if (victim == null) return;
        DamageSource source = new DamageSource(cause == null ? "generic" : cause);
        for (net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AfterDamage callback : snapshot(AFTER_DAMAGE))
            invokeSafely(() -> callback.afterDamage(victim, source, amount), "after damage");
        for (net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AfterDamage callback : snapshot(LEGACY_AFTER_DAMAGE))
            invokeSafely(() -> callback.afterDamage(victim, source, amount), "after damage (legacy)");
        if (died) {
            for (net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AfterDeath callback : snapshot(AFTER_DEATH))
                invokeSafely(() -> callback.afterDeath(victim, source), "after death");
            for (net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AfterDeath callback : snapshot(LEGACY_AFTER_DEATH))
                invokeSafely(() -> callback.afterDeath(victim, source), "after death (legacy)");
        }
    }

    public static boolean onMobSpawn(long entityHandle, double x, double y, double z) {
        if (!bootstrapped) return true;
        Entity entity = Entity.of(entityHandle);
        ServerWorld world = server == null ? null : server.getOverworld();
        for (ModEvents.MobSpawn callback : ModEvents.MOB_SPAWN.snapshot())
            if (!invokeAllow(() -> callback.onMobSpawn(entity, world, x, y, z), "mob spawn")) return false;
        dispatchEntityLoad(entity, world);
        return true;
    }

    public static ActionResult onEntityUse(long playerHandle, long entityHandle, Hand hand) {
        if (!bootstrapped) return ActionResult.PASS;
        ServerPlayerEntity player = ServerPlayerEntity.of(playerHandle);
        ServerWorld world = player.getServerWorld();
        Entity entity = Entity.of(entityHandle);
        return invokeResult(() -> UseEntityCallback.EVENT.invoker().interact(player, world,
            hand == null ? Hand.MAIN_HAND : hand, entity, new EntityHitResult(entity)), "use entity");
    }

    public static ActionResult onItemUse(long playerHandle, Hand hand) {
        if (!bootstrapped) return ActionResult.PASS;
        ServerPlayerEntity player = ServerPlayerEntity.of(playerHandle);
        return invokeResult(() -> UseItemCallback.EVENT.invoker().interact(player, player.getServerWorld(),
            hand == null ? Hand.MAIN_HAND : hand), "use item");
    }

    /** Native play/config plugin-message entrypoint (JILjava/lang/String;[B)V. */
    public static void onPluginMessage(long handle, int phase, String channel, byte[] payload) {
        if (!bootstrapped || channel == null) return;
        Identifier id = Identifier.tryParse(channel);
        if (id == null) return;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerPlayNetworkHandler handler = player == null ? null : player.getNetworkHandler();
        try {
            ServerPlayNetworking.receive(server, player, handler, id,
                new net.minecraft.network.PacketByteBuf(payload == null ? new byte[0] : payload));
        } catch (Throwable failure) {
            // Do not leave a Java exception pending across the native void
            // callback boundary when an optional receiver fails.
            log("ERROR", "plugin message receiver failed (phase " + phase + "): " + failure);
        }
    }

    public static void onBlockBreakResult(long handle, int x, int y, int z, int rawState,
                                          boolean broken) {
        if (!bootstrapped) return;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerWorld world = player.getServerWorld();
        BlockPos pos = new BlockPos(x, y, z);
        BlockState state = new BlockState(rawState);
        if (broken) {
            for (PlayerBlockBreakEvents.After callback : snapshot(PlayerBlockBreakEvents.AFTER.snapshot()))
                invokeSafely(() -> callback.afterBlockBreak(world, player, pos, state, null), "after block break");
        } else {
            for (PlayerBlockBreakEvents.Canceled callback : snapshot(PlayerBlockBreakEvents.CANCELED.snapshot()))
                invokeSafely(() -> callback.onBlockBreakCanceled(world, player, pos, state, null), "canceled block break");
        }
    }

    public static void onPlayerRespawn(long oldHandle, long newHandle, boolean alive) {
        if (!bootstrapped) return;
        ServerPlayerEntity oldPlayer = ServerPlayerEntity.of(oldHandle);
        ServerPlayerEntity newPlayer = ServerPlayerEntity.of(newHandle);
        for (ServerPlayerEvents.CopyFrom callback : snapshot(PLAYER_COPY_FROM))
            invokeSafely(() -> callback.copyFrom(oldPlayer, newPlayer, alive), "player copy");
        for (ServerPlayerEvents.AfterRespawn callback : snapshot(PLAYER_AFTER_RESPAWN))
            invokeSafely(() -> callback.afterRespawn(oldPlayer, newPlayer, alive), "player respawn");
    }

    public static boolean onGameMessage(long playerHandle, String message, boolean overlay) {
        if (!bootstrapped) return true;
        ServerPlayerEntity player = playerHandle == 0L ? null : ServerPlayerEntity.of(playerHandle);
        net.minecraft.text.Text text = net.minecraft.text.Text.literal(message == null ? "" : message);
        if (!invokeAllow(() -> ServerMessageEvents.ALLOW_GAME_MESSAGE.invoker()
                .allowGameMessage(server, player, text, overlay), "allow game message")) return false;
        for (ServerMessageEvents.GameMessage callback : snapshot(ServerMessageEvents.GAME_MESSAGE.snapshot()))
            invokeSafely(() -> callback.onGameMessage(server, player, text, overlay), "game message");
        return true;
    }

    public static void dispatchEntityLoad(Entity entity, ServerWorld world) {
        if (!bootstrapped || entity == null) return;
        for (ServerEntityEvents.Load callback : snapshot(ENTITY_LOAD))
            invokeSafely(() -> callback.onLoad(entity, world), "entity load");
        for (net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents.Load callback : snapshot(LEGACY_ENTITY_LOAD))
            invokeSafely(() -> callback.onLoad(entity, world), "entity load (legacy)");
    }

    public static void dispatchEntityUnload(Entity entity, ServerWorld world) {
        if (!bootstrapped || entity == null) return;
        for (ServerEntityEvents.Unload callback : snapshot(ENTITY_UNLOAD))
            invokeSafely(() -> callback.onUnload(entity, world), "entity unload");
        for (net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents.Unload callback : snapshot(LEGACY_ENTITY_UNLOAD))
            invokeSafely(() -> callback.onUnload(entity, world), "entity unload (legacy)");
    }

    private static <T> void registerCallback(List<T> callbacks, T callback) {
        callbacks.add(Objects.requireNonNull(callback, "callback"));
    }

    public static void registerServerStarted(ServerLifecycleEvents.ServerStarted callback) { registerCallback(SERVER_STARTED, callback); }
    public static void registerServerStarting(ServerLifecycleEvents.ServerStarting callback) { registerCallback(SERVER_STARTING, callback); }
    public static void registerServerStopping(ServerLifecycleEvents.ServerStopping callback) { registerCallback(SERVER_STOPPING, callback); }
    public static void registerServerStopped(ServerLifecycleEvents.ServerStopped callback) { registerCallback(SERVER_STOPPED, callback); }
    public static void registerSyncDataPackContents(ServerLifecycleEvents.SyncDataPackContents callback) { registerCallback(SYNC_DATA_PACK_CONTENTS, callback); }
    public static void registerStartDataPackReload(ServerLifecycleEvents.StartDataPackReload callback) { registerCallback(START_DATA_PACK_RELOAD, callback); }
    public static void registerEndDataPackReload(ServerLifecycleEvents.EndDataPackReload callback) { registerCallback(END_DATA_PACK_RELOAD, callback); }
    public static void registerBeforeSave(ServerLifecycleEvents.BeforeSave callback) { registerCallback(BEFORE_SAVE, callback); }
    public static void registerAfterSave(ServerLifecycleEvents.AfterSave callback) { registerCallback(AFTER_SAVE, callback); }
    public static void registerTickStart(ServerTickEvents.Start callback) { registerCallback(TICK_START, callback); }
    public static void registerTickEnd(ServerTickEvents.End callback) { registerCallback(TICK_END, callback); }
    public static void registerStartServerTick(ServerTickEvents.StartServerTick callback) { registerCallback(SERVER_TICK_START, callback); }
    public static void registerEndServerTick(ServerTickEvents.EndServerTick callback) { registerCallback(SERVER_TICK_END, callback); }
    public static void registerStartWorldTick(ServerTickEvents.StartWorldTick callback) { registerCallback(WORLD_TICK_START, callback); }
    public static void registerEndWorldTick(ServerTickEvents.EndWorldTick callback) { registerCallback(WORLD_TICK_END, callback); }
    public static void registerWorldLoad(ServerWorldEvents.Load callback) { registerCallback(WORLD_LOAD, callback); }
    public static void registerWorldUnload(ServerWorldEvents.Unload callback) { registerCallback(WORLD_UNLOAD, callback); }
    public static void registerPlayerJoin(ServerPlayConnectionEvents.Join callback) { registerCallback(PLAYER_JOIN, callback); }
    public static void registerPlayerQuit(ServerPlayConnectionEvents.Disconnect callback) { registerCallback(PLAYER_QUIT, callback); }
    public static void registerPlayerCopyFrom(ServerPlayerEvents.CopyFrom callback) { registerCallback(PLAYER_COPY_FROM, callback); }
    public static void registerPlayerAfterRespawn(ServerPlayerEvents.AfterRespawn callback) { registerCallback(PLAYER_AFTER_RESPAWN, callback); }
    public static void registerPlayerJoinEvent(ServerPlayerEvents.Join callback) { registerCallback(PLAYER_JOIN_EVENT, callback); }
    public static void registerPlayerLeaveEvent(ServerPlayerEvents.Leave callback) { registerCallback(PLAYER_LEAVE_EVENT, callback); }
    public static void registerEntityLoad(ServerEntityEvents.Load callback) { registerCallback(ENTITY_LOAD, callback); }
    public static void registerEntityUnload(ServerEntityEvents.Unload callback) { registerCallback(ENTITY_UNLOAD, callback); }
    public static void registerAllowDamage(net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AllowDamage callback) { registerCallback(ALLOW_DAMAGE, callback); }
    public static void registerAfterDamage(net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AfterDamage callback) { registerCallback(AFTER_DAMAGE, callback); }
    public static void registerAfterDeath(net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AfterDeath callback) { registerCallback(AFTER_DEATH, callback); }
    public static void registerLegacyEntityLoad(net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents.Load callback) { registerCallback(LEGACY_ENTITY_LOAD, callback); }
    public static void registerLegacyEntityUnload(net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents.Unload callback) { registerCallback(LEGACY_ENTITY_UNLOAD, callback); }
    public static void registerLegacyAllowDamage(net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AllowDamage callback) { registerCallback(LEGACY_ALLOW_DAMAGE, callback); }
    public static void registerLegacyAfterDamage(net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AfterDamage callback) { registerCallback(LEGACY_AFTER_DAMAGE, callback); }
    public static void registerLegacyAfterDeath(net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AfterDeath callback) { registerCallback(LEGACY_AFTER_DEATH, callback); }
    public static void registerAllowChatMessage(ServerMessageEvents.AllowChatMessage callback) { registerCallback(ALLOW_CHAT_MESSAGE, callback); }
    public static void registerChatMessage(ServerMessageEvents.ChatMessage callback) { registerCallback(CHAT_MESSAGE, callback); }
    public static void registerUseBlock(UseBlockCallback callback) { registerCallback(USE_BLOCK, callback); }
    public static void registerAttackBlock(AttackBlockCallback callback) { registerCallback(ATTACK_BLOCK, callback); }
    public static void registerBeforeBreak(PlayerBlockBreakEvents.Before callback) { registerCallback(BEFORE_BREAK, callback); }
    public static void registerCommandRegistration(CommandRegistrationCallback callback) { registerCallback(COMMAND_REGISTRATION, callback); }

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
        SERVER_STARTING.clear();
        SERVER_STARTED.clear();
        SERVER_STOPPING.clear();
        SERVER_STOPPED.clear();
        SYNC_DATA_PACK_CONTENTS.clear();
        START_DATA_PACK_RELOAD.clear();
        END_DATA_PACK_RELOAD.clear();
        SERVER_RELOAD_LISTENERS.clear();
        BEFORE_SAVE.clear();
        AFTER_SAVE.clear();
        TICK_START.clear();
        TICK_END.clear();
        SERVER_TICK_START.clear();
        SERVER_TICK_END.clear();
        WORLD_TICK_START.clear();
        WORLD_TICK_END.clear();
        WORLD_LOAD.clear();
        WORLD_UNLOAD.clear();
        PLAYER_JOIN.clear();
        PLAYER_QUIT.clear();
        PLAYER_COPY_FROM.clear();
        PLAYER_AFTER_RESPAWN.clear();
        PLAYER_JOIN_EVENT.clear();
        PLAYER_LEAVE_EVENT.clear();
        ENTITY_LOAD.clear();
        ENTITY_UNLOAD.clear();
        ALLOW_DAMAGE.clear();
        AFTER_DAMAGE.clear();
        AFTER_DEATH.clear();
        LEGACY_ENTITY_LOAD.clear();
        LEGACY_ENTITY_UNLOAD.clear();
        LEGACY_ALLOW_DAMAGE.clear();
        LEGACY_AFTER_DAMAGE.clear();
        LEGACY_AFTER_DEATH.clear();
        ALLOW_CHAT_MESSAGE.clear();
        CHAT_MESSAGE.clear();
        USE_BLOCK.clear();
        ATTACK_BLOCK.clear();
        BEFORE_BREAK.clear();
        COMMAND_REGISTRATION.clear();
        ServerLifecycleEvents.clear();
        ServerTickEvents.clear();
        ServerWorldEvents.clear();
        ServerPlayConnectionEvents.clear();
        UseBlockCallback.clear();
        AttackBlockCallback.clear();
        PlayerBlockBreakEvents.clear();
        net.fabricmc.fabric.api.event.player.AttackEntityCallback.clear();
        net.fabricmc.fabric.api.event.player.UseEntityCallback.clear();
        net.fabricmc.fabric.api.event.player.UseItemCallback.clear();
        net.fabricmc.fabric.api.entity.event.v1.ServerPlayerEvents.clear();
        net.fabricmc.fabric.api.entity.event.v1.ServerEntityEvents.clear();
        net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.clear();
        net.fabricmc.fabric.api.entity.event.v1.EntityElytraEvents.clear();
        net.fabricmc.fabric.api.entity.event.v1.EntitySleepEvents.clear();
        net.fabricmc.fabric.api.entity.event.v1.ServerEntityCombatEvents.clear();
        net.fabricmc.fabric.api.entity.event.v1.ServerEntityWorldChangeEvents.clear();
        net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents.clear();
        net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.clear();
        net.fabricmc.fabric.api.event.lifecycle.v1.CommonLifecycleEvents.clear();
        net.fabricmc.fabric.api.event.lifecycle.v1.ServerBlockEntityEvents.clear();
        net.fabricmc.fabric.api.event.lifecycle.v1.ServerChunkEvents.clear();
        net.fabricmc.fabric.api.event.player.PlayerPickItemEvents.clear();
        ServerMessageEvents.clear();
        CommandRegistrationCallback.clear();
        ServerPlayNetworking.clear();
        net.fabricmc.fabric.api.networking.v1.ServerConfigurationNetworking.clear();
        net.fabricmc.fabric.api.networking.v1.ServerLoginNetworking.clear();
        net.fabricmc.fabric.api.networking.v1.ServerConfigurationConnectionEvents.clear();
        net.fabricmc.fabric.api.networking.v1.ServerLoginConnectionEvents.clear();
        net.fabricmc.fabric.api.networking.v1.S2CConfigurationChannelEvents.clear();
        net.fabricmc.fabric.api.networking.v1.S2CPlayChannelEvents.clear();
        net.fabricmc.fabric.api.networking.v1.EntityTrackingEvents.clear();
        net.fabricmc.fabric.api.itemgroup.v1.ItemGroupEvents.clear();
        net.fabricmc.fabric.api.attachment.v1.AttachmentRegistry.clear();
        restoreLoaderMetadata();
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
    private static void registerMixins(List<Candidate> candidates) throws Exception {
        MixinHooks.clear();
        for (Candidate candidate : candidates) {
            for (String config : candidate.mixinConfigs) {
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
                         " from " + candidate.id + ": " + failure);
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

    public static void registerTransformedMethod(String owner, String name, String descriptor) {
        NativeBridge.nativeRegisterTransformedMethod(owner, name, descriptor);
    }

    private static void loadMods(Path directory) throws Exception {
        if (!Files.isDirectory(directory)) {
            log("INFO", "mods directory not present: " + directory);
            configureLoaderMetadata(List.of());
            return;
        }
        List<Path> roots;
        try (var stream = Files.list(directory)) {
            roots = stream
                .filter(path -> Files.isDirectory(path) || path.toString().endsWith(".jar"))
                .sorted(Comparator.comparing(Path::toString)).toList();
        }
        if (modExpansion != null) modExpansion.close();
        modExpansion = NestedJarSupport.expand(roots);

        List<Candidate> candidates = new ArrayList<>();
        for (Path path : modExpansion.paths()) {
            try {
                Candidate candidate = readCandidate(path);
                if (candidate == null) continue;
                if (candidate.environmentMatchesServer()) candidates.add(candidate);
                else log("INFO", "skipping client-only mod " + candidate.id);
            } catch (Exception e) {
                log("ERROR", "ignoring invalid mod metadata " + path + ": " + e);
                if (Boolean.getBoolean("cppfm.jvm.strict"))
                    throw new IllegalArgumentException("invalid mod metadata: " + path, e);
            }
        }
        Map<String, Candidate> byId = new LinkedHashMap<>();
        for (Candidate candidate : candidates) {
            if (byId.put(candidate.id, candidate) != null)
                throw new IllegalArgumentException("duplicate mod id: " + candidate.id);
        }
        for (Candidate candidate : candidates) {
            for (String provided : candidate.provides) {
                Candidate previous = byId.putIfAbsent(provided, candidate);
                if (previous != null && previous != candidate)
                    throw new IllegalArgumentException("duplicate provided mod id: " + provided);
            }
        }
        validateDependencies(candidates, byId);
        List<Candidate> order = new ArrayList<>();
        Set<String> visiting = new HashSet<>();
        Set<String> visited = new HashSet<>();
        for (Candidate candidate : candidates) visit(candidate, byId, visiting, visited, order);

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
        previousContextClassLoader = Thread.currentThread().getContextClassLoader();
        Thread.currentThread().setContextClassLoader(modLoader);
        int initializedEntrypoints = 0;
        // Mixin metadata must be registered before the first target class is
        // resolved.  The KnotClassLoader created by KnotLauncher has already
        // indexed these configs, so target definitions can be transformed on
        // their first load rather than being patched after the fact.
        configureLoaderMetadata(candidates);
        registerMixins(order);
        // Fabric initializes server entrypoints before DedicatedServer creates
        // its command manager.  Preserve that ordering so constructor-return
        // mixins (notably Carpet's command registration hook) observe their
        // initialized mod state instead of an empty settings registry.
        for (Candidate candidate : order) initializedEntrypoints += initialize(candidate);
        server = MinecraftServer.of(NativeAccess.serverHandle());
        for (CommandRegistrationCallback callback : snapshot(COMMAND_REGISTRATION))
            invokeSafely(() -> callback.register(server.getCommandManager().getDispatcher(),
                new net.minecraft.command.CommandRegistryAccess(),
                net.minecraft.server.command.CommandManager.RegistrationEnvironment.DEDICATED),
                "command registration");
        NativeBridge.nativeSetModStats(candidates.size(), initializedEntrypoints);
        log("INFO", "loaded " + candidates.size() + " mod candidate(s), initialized " + initializedEntrypoints + " entrypoint(s)");
    }

    private static void fireWorldLoad() {
        ServerWorld world = ServerWorld.of(NativeAccess.serverWorld(0), server);
        for (ServerWorldEvents.Load callback : snapshot(WORLD_LOAD))
            invokeSafely(() -> callback.onWorldLoad(server, world), "world load");
    }

    private static void fireWorldUnload() {
        ServerWorld world = server == null ? null : server.getOverworld();
        for (ServerWorldEvents.Unload callback : snapshot(WORLD_UNLOAD))
            invokeSafely(() -> callback.onWorldUnload(server, world), "world unload");
    }

    private static void visit(Candidate candidate, Map<String, Candidate> byId,
                              Set<String> visiting, Set<String> visited,
                              List<Candidate> order) {
        if (visited.contains(candidate.id)) return;
        if (!visiting.add(candidate.id)) throw new IllegalArgumentException("cyclic mod dependency at " + candidate.id);
        for (String dependency : candidate.dependencies.keySet()) {
            if (BUILTIN_DEPENDENCIES.contains(dependency)) continue;
            Candidate required = byId.get(dependency);
            if (required == null) throw new IllegalArgumentException(candidate.id + " requires missing mod " + dependency);
            visit(required, byId, visiting, visited, order);
        }
        visiting.remove(candidate.id);
        visited.add(candidate.id);
        order.add(candidate);
    }

    private static int initialize(Candidate candidate) throws Exception {
        int initialized = 0;
        for (Entrypoint entrypointDefinition : candidate.entrypoints) {
            String entrypoint = entrypointDefinition.definition;
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
                    registerLoaderEntrypoint(entrypointDefinition.key, instance,
                        candidate.id, entrypoint);
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
            registerLoaderEntrypoint(entrypointDefinition.key, instance,
                candidate.id, entrypoint);
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

    @SuppressWarnings("unchecked")
    private static Candidate readCandidate(Path path) throws Exception {
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
        if (!(parsed instanceof Map<?, ?> raw)) throw new IllegalArgumentException("fabric.mod.json is not an object");
        Map<String, Object> json = (Map<String, Object>) raw;
        String id = string(json.get("id"));
        if (id == null || !id.matches("[a-z][a-z0-9_-]{1,63}")) throw new IllegalArgumentException("invalid mod id");
        List<Entrypoint> entrypoints = new ArrayList<>();
        Object entrypointObject = json.get("entrypoints");
        if (entrypointObject instanceof Map<?, ?> entries) {
            addEntrypoints(entrypoints, entries.get("server"), "server");
            addEntrypoints(entrypoints, entries.get("main"), "main");
        }
        Map<String, String> dependencies = dependencyMap(json.get("depends"), "depends");
        Map<String, String> recommends = dependencyMap(json.get("recommends"), "recommends");
        Map<String, String> suggests = dependencyMap(json.get("suggests"), "suggests");
        Map<String, String> breaks = dependencyMap(json.get("breaks"), "breaks");
        Map<String, String> conflicts = dependencyMap(json.get("conflicts"), "conflicts");
        List<String> provides = stringList(json.get("provides"), "provides");
        List<String> mixinConfigs = new ArrayList<>();
        Object mixins = json.get("mixins");
        if (mixins instanceof String config) mixinConfigs.add(config);
        else if (mixins instanceof List<?> list)
            for (Object config : list) {
                if (config instanceof String value) mixinConfigs.add(value);
                else if (config instanceof Map<?, ?> object && object.get("config") instanceof String value)
                    mixinConfigs.add(value);
            }
        String environment = string(json.get("environment"));
        if (environment == null || environment.isBlank()) environment = "*";
        return new Candidate(id, string(json.get("name")), string(json.get("version")),
            environment, path, dependencies, recommends, suggests, breaks, conflicts,
            provides, entrypoints, mixinConfigs);
    }

    @SuppressWarnings("unchecked")
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
                        throw new IllegalArgumentException(key + " has an invalid requirement for " + id);
                    values.add(string);
                }
                if (values.isEmpty())
                    throw new IllegalArgumentException(key + " has an invalid requirement for " + id);
                // Fabric metadata uses an array for alternative requirements. The
                // version matcher already implements the pipe-separated OR form.
                requirement = String.join(" | ", values);
            } else {
                throw new IllegalArgumentException(key + " has an invalid requirement for " + id);
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

    private static void validateDependencies(List<Candidate> candidates,
                                             Map<String, Candidate> byId) {
        for (Candidate candidate : candidates) {
            for (Map.Entry<String, String> dependency : candidate.dependencies.entrySet()) {
                String actual = BUILTIN_DEPENDENCIES.contains(dependency.getKey())
                    ? BUILTIN_VERSIONS.get(dependency.getKey())
                    : versionOf(byId.get(dependency.getKey()));
                if (actual == null)
                    throw new IllegalArgumentException(candidate.id + " requires missing mod " + dependency.getKey());
                if (!matchesLoaderVersion(actual, dependency.getValue()))
                    throw new IllegalArgumentException(candidate.id + " requires " + dependency.getKey()
                        + " " + dependency.getValue() + " but found " + actual);
            }
            validateConflicts(candidate, candidate.breaks, byId, "breaks");
            validateConflicts(candidate, candidate.conflicts, byId, "conflicts");
        }
    }

    private static void validateConflicts(Candidate candidate, Map<String, String> conflicts,
                                          Map<String, Candidate> byId, String kind) {
        for (Map.Entry<String, String> conflict : conflicts.entrySet()) {
            String actual = BUILTIN_DEPENDENCIES.contains(conflict.getKey())
                ? BUILTIN_VERSIONS.get(conflict.getKey())
                : versionOf(byId.get(conflict.getKey()));
            if (actual != null && matchesLoaderVersion(actual, conflict.getValue()))
                throw new IllegalArgumentException(candidate.id + " " + kind + " " + conflict.getKey()
                    + " " + conflict.getValue() + " (found " + actual + ")");
        }
    }

    private static String versionOf(Candidate candidate) {
        return candidate == null || candidate.version == null || candidate.version.isBlank()
            ? null : candidate.version;
    }

    private static String javaMajorVersion() {
        String value = System.getProperty("java.specification.version", "0");
        if (value.startsWith("1.")) value = value.substring(2);
        int dot = value.indexOf('.');
        return dot < 0 ? value : value.substring(0, dot);
    }

    private static void addEntrypoints(List<Entrypoint> output, Object value, String key) {
        if (value instanceof String string) output.add(new Entrypoint(key, string));
        else if (value instanceof List<?> list)
            for (Object entry : list) {
                if (entry instanceof String string) output.add(new Entrypoint(key, string));
                else if (entry instanceof Map<?, ?> map && map.get("value") instanceof String string)
                    output.add(new Entrypoint(key, string));
            }
        else if (value instanceof Map<?, ?> map && map.get("value") instanceof String string)
            output.add(new Entrypoint(key, string));
    }

    private static String string(Object value) { return value instanceof String ? (String) value : null; }

    private static void configureLoaderMetadata(List<Candidate> candidates) {
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
        for (Candidate candidate : candidates) {
            versions.put(candidate.id, candidate.version);
            registerLoaderMod(candidate.id, candidate.name, candidate.version, candidate.path,
                candidate.environment, candidate.provides, candidate.dependencies,
                candidate.recommends, candidate.suggests, candidate.conflicts, candidate.breaks);
        }
        saveLoaderProperty("cppfm.loaded.mods", String.join(",", versions.keySet()));
        for (Map.Entry<String, String> entry : versions.entrySet()) {
            saveLoaderProperty("cppfm.mod.version." + entry.getKey(), entry.getValue() == null ? "" : entry.getValue());
            Candidate candidate = candidates.stream().filter(item -> item.id.equals(entry.getKey())).findFirst().orElse(null);
            if (candidate != null) {
                saveLoaderProperty("cppfm.mod.name." + candidate.id, candidate.name);
                saveLoaderProperty("cppfm.mod.root." + candidate.id, candidate.path.toString());
                saveLoaderProperty("cppfm.mod.environment." + candidate.id, candidate.environment);
                saveLoaderProperty("cppfm.mod.provides." + candidate.id, String.join(",", candidate.provides));
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
        try {
            if (modLoader instanceof URLClassLoader urls &&
                modLoader != CppModRuntime.class.getClassLoader()) {
                try { urls.close(); }
                catch (IOException e) { log("WARN", "mod classloader close failed: " + e); }
            }
        } finally {
            modLoader = null;
            if (previousContextClassLoader != null) {
                Thread.currentThread().setContextClassLoader(previousContextClassLoader);
                previousContextClassLoader = null;
            }
            if (modExpansion != null) {
                modExpansion.close();
                modExpansion = null;
            }
        }
    }

    private static void invokeSafely(Runnable action, String operation) {
        try { action.run(); } catch (Throwable failure) { log("ERROR", operation + " failed: " + failure); }
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

    private record Candidate(String id, String name, String version, String environment, Path path,
                             Map<String, String> dependencies, Map<String, String> recommends,
                             Map<String, String> suggests, Map<String, String> breaks,
                             Map<String, String> conflicts, List<String> provides,
                             List<Entrypoint> entrypoints, List<String> mixinConfigs) {
        private boolean environmentMatchesServer() {
            return environment == null || environment.equals("*")
                || environment.equalsIgnoreCase("server")
                || environment.equalsIgnoreCase("universal");
        }
    }

    private record Entrypoint(String key, String definition) {}
}
