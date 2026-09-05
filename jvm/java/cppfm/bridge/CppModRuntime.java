package cppfm.bridge;

import cppfm.api.ModEvents;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
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
import net.fabricmc.fabric.api.event.player.AttackEntityCallback;
import net.fabricmc.fabric.api.event.player.PlayerBlockBreakEvents;
import net.fabricmc.fabric.api.event.player.UseBlockCallback;
import net.fabricmc.fabric.api.event.player.UseEntityCallback;
import net.fabricmc.fabric.api.event.player.UseItemCallback;
import net.fabricmc.fabric.api.networking.v1.PacketSender;
import net.fabricmc.fabric.api.networking.v1.ServerPlayConnectionEvents;
import net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking;
import net.fabricmc.fabric.api.message.v1.ServerMessageEvents;
import net.minecraft.block.BlockState;
import net.minecraft.entity.DamageSource;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.network.message.MessageType;
import net.minecraft.network.message.SignedMessage;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.ActionResult;
import net.minecraft.util.Hand;
import net.minecraft.util.Identifier;
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
    private static final List<ServerLifecycleEvents.ServerStarted> SERVER_STARTED = new ArrayList<>();
    private static final List<ServerLifecycleEvents.ServerStarting> SERVER_STARTING = new ArrayList<>();
    private static final List<ServerLifecycleEvents.ServerStopping> SERVER_STOPPING = new ArrayList<>();
    private static final List<ServerLifecycleEvents.ServerStopped> SERVER_STOPPED = new ArrayList<>();
    private static final List<ServerTickEvents.Start> TICK_START = new ArrayList<>();
    private static final List<ServerTickEvents.End> TICK_END = new ArrayList<>();
    private static final List<ServerTickEvents.StartServerTick> SERVER_TICK_START = new ArrayList<>();
    private static final List<ServerTickEvents.EndServerTick> SERVER_TICK_END = new ArrayList<>();
    private static final List<ServerTickEvents.StartWorldTick> WORLD_TICK_START = new ArrayList<>();
    private static final List<ServerTickEvents.EndWorldTick> WORLD_TICK_END = new ArrayList<>();
    private static final List<ServerWorldEvents.Load> WORLD_LOAD = new ArrayList<>();
    private static final List<ServerWorldEvents.Unload> WORLD_UNLOAD = new ArrayList<>();
    private static final List<ServerPlayConnectionEvents.Join> PLAYER_JOIN = new ArrayList<>();
    private static final List<ServerPlayConnectionEvents.Disconnect> PLAYER_QUIT = new ArrayList<>();
    private static final List<ServerPlayerEvents.CopyFrom> PLAYER_COPY_FROM = new ArrayList<>();
    private static final List<ServerPlayerEvents.AfterRespawn> PLAYER_AFTER_RESPAWN = new ArrayList<>();
    private static final List<ServerPlayerEvents.Join> PLAYER_JOIN_EVENT = new ArrayList<>();
    private static final List<ServerPlayerEvents.Leave> PLAYER_LEAVE_EVENT = new ArrayList<>();
    private static final List<ServerEntityEvents.Load> ENTITY_LOAD = new ArrayList<>();
    private static final List<ServerEntityEvents.Unload> ENTITY_UNLOAD = new ArrayList<>();
    private static final List<net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AllowDamage> ALLOW_DAMAGE = new ArrayList<>();
    private static final List<net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AfterDamage> AFTER_DAMAGE = new ArrayList<>();
    private static final List<net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AfterDeath> AFTER_DEATH = new ArrayList<>();
    private static final List<net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents.Load> LEGACY_ENTITY_LOAD = new ArrayList<>();
    private static final List<net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents.Unload> LEGACY_ENTITY_UNLOAD = new ArrayList<>();
    private static final List<net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AllowDamage> LEGACY_ALLOW_DAMAGE = new ArrayList<>();
    private static final List<net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AfterDamage> LEGACY_AFTER_DAMAGE = new ArrayList<>();
    private static final List<net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AfterDeath> LEGACY_AFTER_DEATH = new ArrayList<>();
    private static final List<ServerMessageEvents.AllowChatMessage> ALLOW_CHAT_MESSAGE = new ArrayList<>();
    private static final List<ServerMessageEvents.ChatMessage> CHAT_MESSAGE = new ArrayList<>();
    private static final List<UseBlockCallback> USE_BLOCK = new ArrayList<>();
    private static final List<AttackBlockCallback> ATTACK_BLOCK = new ArrayList<>();
    private static final List<PlayerBlockBreakEvents.Before> BEFORE_BREAK = new ArrayList<>();
    private static final List<CommandRegistrationCallback> COMMAND_REGISTRATION = new ArrayList<>();
    private static ClassLoader modLoader;
    private static MinecraftServer server;
    private static boolean bootstrapped;

    private CppModRuntime() {}

    public static synchronized boolean bootstrap(String modsDir, String configDir) {
        if (bootstrapped) return true;
        try {
            loadMods(Paths.get(modsDir));
            if (server == null) server = MinecraftServer.of(NativeBridge.serverHandle());
            bootstrapped = true;
            for (ServerLifecycleEvents.ServerStarting callback : snapshot(SERVER_STARTING))
                invokeSafely(() -> callback.onServerStarting(server), "server starting");
            fireWorldLoad();
            for (ServerLifecycleEvents.ServerStarted callback : snapshot(SERVER_STARTED))
                invokeSafely(() -> callback.onServerStarted(server), "server started");
            return true;
        } catch (Throwable failure) {
            log("ERROR", "mod bootstrap failed: " + failure);
            if (Boolean.getBoolean("cppfm.jvm.strict")) return false;
            // A malformed optional mod must not make the native server vanish.
            bootstrapped = true;
            return true;
        }
    }

    public static synchronized void shutdown() {
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
        closeModLoader();
        bootstrapped = false;
        server = null;
    }

    public static void onServerTick(long tick) {
        if (!bootstrapped) return;
        if (server != null) server.setTick(tick);
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
    }

    public static void onPlayerJoin(long handle) {
        if (!bootstrapped) return;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerPlayNetworkHandler network = player.getNetworkHandler();
        for (ServerPlayConnectionEvents.Join callback : snapshot(PLAYER_JOIN))
            invokeSafely(() -> callback.onPlayReady(network, ServerPlayNetworking.getSender(player), server), "player join");
        for (ServerPlayerEvents.Join callback : snapshot(PLAYER_JOIN_EVENT))
            invokeSafely(() -> callback.onPlayReady(network, player), "player entity join");
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
                                                    new BlockHitResult(pos)), "attack block");
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
        // Commands registered through Fabric's CommandRegistrationCallback
        // must be consumed here, otherwise the native command dispatcher
        // would run a second, unrelated command tree.
        if (server != null && server.getCommandManager().hasCommand(current)) {
            server.getCommandManager().execute(
                current, new net.minecraft.server.command.ServerCommandSource(player, server));
            for (ServerMessageEvents.CommandMessage callback : snapshot(ServerMessageEvents.COMMAND_MESSAGE.snapshot()))
                invokeSafely(() -> callback.onCommandMessage(commandMessage, player, MessageType.Parameters.EMPTY), "command message");
            return null;
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
            for (PlayerBlockBreakEvents.After callback : snapshot(PlayerBlockBreakEvents.CANCELED.snapshot()))
                invokeSafely(() -> callback.afterBlockBreak(world, player, pos, state, null), "canceled block break");
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

    public static void registerServerStarted(ServerLifecycleEvents.ServerStarted callback) { synchronized (SERVER_STARTED) { SERVER_STARTED.add(callback); } }
    public static void registerServerStarting(ServerLifecycleEvents.ServerStarting callback) { synchronized (SERVER_STARTING) { SERVER_STARTING.add(callback); } }
    public static void registerServerStopping(ServerLifecycleEvents.ServerStopping callback) { synchronized (SERVER_STOPPING) { SERVER_STOPPING.add(callback); } }
    public static void registerServerStopped(ServerLifecycleEvents.ServerStopped callback) { synchronized (SERVER_STOPPED) { SERVER_STOPPED.add(callback); } }
    public static void registerTickStart(ServerTickEvents.Start callback) { synchronized (TICK_START) { TICK_START.add(callback); } }
    public static void registerTickEnd(ServerTickEvents.End callback) { synchronized (TICK_END) { TICK_END.add(callback); } }
    public static void registerStartServerTick(ServerTickEvents.StartServerTick callback) { synchronized (SERVER_TICK_START) { SERVER_TICK_START.add(callback); } }
    public static void registerEndServerTick(ServerTickEvents.EndServerTick callback) { synchronized (SERVER_TICK_END) { SERVER_TICK_END.add(callback); } }
    public static void registerStartWorldTick(ServerTickEvents.StartWorldTick callback) { synchronized (WORLD_TICK_START) { WORLD_TICK_START.add(callback); } }
    public static void registerEndWorldTick(ServerTickEvents.EndWorldTick callback) { synchronized (WORLD_TICK_END) { WORLD_TICK_END.add(callback); } }
    public static void registerWorldLoad(ServerWorldEvents.Load callback) { synchronized (WORLD_LOAD) { WORLD_LOAD.add(callback); } }
    public static void registerWorldUnload(ServerWorldEvents.Unload callback) { synchronized (WORLD_UNLOAD) { WORLD_UNLOAD.add(callback); } }
    public static void registerPlayerJoin(ServerPlayConnectionEvents.Join callback) { synchronized (PLAYER_JOIN) { PLAYER_JOIN.add(callback); } }
    public static void registerPlayerQuit(ServerPlayConnectionEvents.Disconnect callback) { synchronized (PLAYER_QUIT) { PLAYER_QUIT.add(callback); } }
    public static void registerPlayerCopyFrom(ServerPlayerEvents.CopyFrom callback) { synchronized (PLAYER_COPY_FROM) { PLAYER_COPY_FROM.add(callback); } }
    public static void registerPlayerAfterRespawn(ServerPlayerEvents.AfterRespawn callback) { synchronized (PLAYER_AFTER_RESPAWN) { PLAYER_AFTER_RESPAWN.add(callback); } }
    public static void registerPlayerJoinEvent(ServerPlayerEvents.Join callback) { synchronized (PLAYER_JOIN_EVENT) { PLAYER_JOIN_EVENT.add(callback); } }
    public static void registerPlayerLeaveEvent(ServerPlayerEvents.Leave callback) { synchronized (PLAYER_LEAVE_EVENT) { PLAYER_LEAVE_EVENT.add(callback); } }
    public static void registerEntityLoad(ServerEntityEvents.Load callback) { synchronized (ENTITY_LOAD) { ENTITY_LOAD.add(callback); } }
    public static void registerEntityUnload(ServerEntityEvents.Unload callback) { synchronized (ENTITY_UNLOAD) { ENTITY_UNLOAD.add(callback); } }
    public static void registerAllowDamage(net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AllowDamage callback) { synchronized (ALLOW_DAMAGE) { ALLOW_DAMAGE.add(callback); } }
    public static void registerAfterDamage(net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AfterDamage callback) { synchronized (AFTER_DAMAGE) { AFTER_DAMAGE.add(callback); } }
    public static void registerAfterDeath(net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents.AfterDeath callback) { synchronized (AFTER_DEATH) { AFTER_DEATH.add(callback); } }
    public static void registerLegacyEntityLoad(net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents.Load callback) { synchronized (LEGACY_ENTITY_LOAD) { LEGACY_ENTITY_LOAD.add(callback); } }
    public static void registerLegacyEntityUnload(net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents.Unload callback) { synchronized (LEGACY_ENTITY_UNLOAD) { LEGACY_ENTITY_UNLOAD.add(callback); } }
    public static void registerLegacyAllowDamage(net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AllowDamage callback) { synchronized (LEGACY_ALLOW_DAMAGE) { LEGACY_ALLOW_DAMAGE.add(callback); } }
    public static void registerLegacyAfterDamage(net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AfterDamage callback) { synchronized (LEGACY_AFTER_DAMAGE) { LEGACY_AFTER_DAMAGE.add(callback); } }
    public static void registerLegacyAfterDeath(net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.AfterDeath callback) { synchronized (LEGACY_AFTER_DEATH) { LEGACY_AFTER_DEATH.add(callback); } }
    public static void registerAllowChatMessage(ServerMessageEvents.AllowChatMessage callback) { synchronized (ALLOW_CHAT_MESSAGE) { ALLOW_CHAT_MESSAGE.add(callback); } }
    public static void registerChatMessage(ServerMessageEvents.ChatMessage callback) { synchronized (CHAT_MESSAGE) { CHAT_MESSAGE.add(callback); } }
    public static void registerUseBlock(UseBlockCallback callback) { synchronized (USE_BLOCK) { USE_BLOCK.add(callback); } }
    public static void registerAttackBlock(AttackBlockCallback callback) { synchronized (ATTACK_BLOCK) { ATTACK_BLOCK.add(callback); } }
    public static void registerBeforeBreak(PlayerBlockBreakEvents.Before callback) { synchronized (BEFORE_BREAK) { BEFORE_BREAK.add(callback); } }
    public static void registerCommandRegistration(CommandRegistrationCallback callback) { synchronized (COMMAND_REGISTRATION) { COMMAND_REGISTRATION.add(callback); } }

    private static void clearRegistrations() {
        SERVER_STARTING.clear();
        SERVER_STARTED.clear();
        SERVER_STOPPING.clear();
        SERVER_STOPPED.clear();
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
        net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents.clear();
        net.fabricmc.fabric.api.event.lifecycle.v1.ServerLivingEntityEvents.clear();
        ServerMessageEvents.clear();
        CommandRegistrationCallback.clear();
        ServerPlayNetworking.clear();
        net.fabricmc.fabric.api.itemgroup.v1.ItemGroupEvents.clear();
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
                        Class<?> mixinClass = Class.forName(className, true, modLoader);
                        MixinHooks.registerMixinClass(mixinClass);
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
            if (name.indexOf('.') >= 0) output.add(name);
            else output.add(prefix.isEmpty() ? name : prefix + "." + name);
        }
    }

    public static void registerTransformedMethod(String owner, String name, String descriptor) {
        NativeBridge.nativeRegisterTransformedMethod(owner, name, descriptor);
    }

    private static void loadMods(Path directory) throws Exception {
        if (!Files.isDirectory(directory)) {
            log("INFO", "mods directory not present: " + directory);
            return;
        }
        List<Candidate> candidates = new ArrayList<>();
        try (var stream = Files.list(directory)) {
            stream.filter(path -> Files.isDirectory(path) || path.toString().endsWith(".jar"))
                  .sorted(Comparator.comparing(Path::toString))
                  .forEach(path -> {
                      try {
                          Candidate candidate = readCandidate(path);
                          if (candidate != null) candidates.add(candidate);
                      } catch (Exception e) {
                          log("ERROR", "ignoring invalid mod metadata " + path + ": " + e);
                          if (Boolean.getBoolean("cppfm.jvm.strict"))
                              throw new IllegalArgumentException("invalid mod metadata: " + path, e);
                      }
                  });
        }
        Map<String, Candidate> byId = new LinkedHashMap<>();
        for (Candidate candidate : candidates) {
            if (byId.put(candidate.id, candidate) != null)
                throw new IllegalArgumentException("duplicate mod id: " + candidate.id);
        }
        List<Candidate> order = new ArrayList<>();
        Set<String> visiting = new HashSet<>();
        Set<String> visited = new HashSet<>();
        for (Candidate candidate : candidates) visit(candidate, byId, visiting, visited, order);

        List<URL> urls = new ArrayList<>();
        for (Candidate candidate : candidates) urls.add(candidate.path.toUri().toURL());
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
        int initializedEntrypoints = 0;
        // Mixin metadata must be registered before the first target class is
        // resolved.  The KnotClassLoader created by KnotLauncher has already
        // indexed these configs, so target definitions can be transformed on
        // their first load rather than being patched after the fact.
        registerMixins(order);
        server = MinecraftServer.of(NativeBridge.serverHandle());
        for (Candidate candidate : order) initializedEntrypoints += initialize(candidate);
        for (CommandRegistrationCallback callback : snapshot(COMMAND_REGISTRATION))
            invokeSafely(() -> callback.register(server.getCommandManager().getDispatcher(),
                new net.minecraft.server.command.CommandRegistryAccess(),
                net.minecraft.server.command.CommandManager.RegistrationEnvironment.DEDICATED),
                "command registration");
        NativeBridge.nativeSetModStats(candidates.size(), initializedEntrypoints);
        log("INFO", "loaded " + candidates.size() + " mod candidate(s), initialized " + initializedEntrypoints + " entrypoint(s)");
    }

    private static void fireWorldLoad() {
        ServerWorld world = ServerWorld.of(NativeBridge.nativeServerWorld(0), server);
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
        for (String dependency : candidate.dependencies) {
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
        for (String entrypoint : candidate.entrypoints) {
            Class<?> type = Class.forName(entrypoint, true, modLoader);
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
        List<String> entrypoints = new ArrayList<>();
        Object entrypointObject = json.get("entrypoints");
        if (entrypointObject instanceof Map<?, ?> entries) {
            addEntrypoints(entrypoints, entries.get("server"));
            addEntrypoints(entrypoints, entries.get("main"));
        }
        List<String> dependencies = new ArrayList<>();
        Object dependencyObject = json.get("depends");
        if (dependencyObject instanceof Map<?, ?> map)
            for (Object key : map.keySet()) dependencies.add(String.valueOf(key));
        List<String> mixinConfigs = new ArrayList<>();
        Object mixins = json.get("mixins");
        if (mixins instanceof String config) mixinConfigs.add(config);
        else if (mixins instanceof List<?> list)
            for (Object config : list) if (config instanceof String value) mixinConfigs.add(value);
        return new Candidate(id, path, dependencies, entrypoints, mixinConfigs);
    }

    private static void addEntrypoints(List<String> output, Object value) {
        if (value instanceof String string) output.add(string);
        else if (value instanceof List<?> list)
            for (Object entry : list) {
                if (entry instanceof String string) output.add(string);
                else if (entry instanceof Map<?, ?> map && map.get("value") instanceof String string)
                    output.add(string);
            }
        else if (value instanceof Map<?, ?> map && map.get("value") instanceof String string)
            output.add(string);
    }

    private static String string(Object value) { return value instanceof String ? (String) value : null; }

    private static <T> List<T> snapshot(List<T> list) {
        synchronized (list) { return List.copyOf(list); }
    }

    private static void closeModLoader() {
        if (!(modLoader instanceof URLClassLoader urls) ||
            modLoader == CppModRuntime.class.getClassLoader()) {
            modLoader = null;
            return;
        }
        try { urls.close(); } catch (IOException e) { log("WARN", "mod classloader close failed: " + e); }
        modLoader = null;
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

    private record Candidate(String id, Path path, List<String> dependencies,
                             List<String> entrypoints, List<String> mixinConfigs) {}
}
