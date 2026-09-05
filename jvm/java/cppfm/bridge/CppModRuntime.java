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
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerWorldEvents;
import net.fabricmc.fabric.api.event.player.AttackBlockCallback;
import net.fabricmc.fabric.api.event.player.PlayerBlockBreakEvents;
import net.fabricmc.fabric.api.event.player.UseBlockCallback;
import net.fabricmc.fabric.api.networking.v1.PacketSender;
import net.fabricmc.fabric.api.networking.v1.ServerPlayConnectionEvents;
import net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking;
import net.minecraft.block.BlockState;
import net.minecraft.entity.Entity;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.ActionResult;
import net.minecraft.util.Hand;
import net.minecraft.util.hit.BlockHitResult;
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
    private static final List<ServerWorldEvents.Load> WORLD_LOAD = new ArrayList<>();
    private static final List<ServerWorldEvents.Unload> WORLD_UNLOAD = new ArrayList<>();
    private static final List<ServerPlayConnectionEvents.Join> PLAYER_JOIN = new ArrayList<>();
    private static final List<ServerPlayConnectionEvents.Disconnect> PLAYER_QUIT = new ArrayList<>();
    private static final List<UseBlockCallback> USE_BLOCK = new ArrayList<>();
    private static final List<AttackBlockCallback> ATTACK_BLOCK = new ArrayList<>();
    private static final List<PlayerBlockBreakEvents.Before> BEFORE_BREAK = new ArrayList<>();
    private static final List<CommandRegistrationCallback> COMMAND_REGISTRATION = new ArrayList<>();
    private static URLClassLoader modLoader;
    private static MinecraftServer server;
    private static boolean bootstrapped;

    private CppModRuntime() {}

    public static synchronized boolean bootstrap(String modsDir, String configDir) {
        if (bootstrapped) return true;
        try {
            server = MinecraftServer.of(NativeBridge.serverHandle());
            log("WARN", "using cppfm compatibility loader fallback; Fabric Loader/Knot is not embedded");
            loadMods(Paths.get(modsDir));
            bootstrapped = true;
            for (ServerLifecycleEvents.ServerStarting callback : snapshot(SERVER_STARTING))
                invokeSafely(() -> callback.onServerStarting(server), "server starting");
            fireWorldLoad();
            for (ServerLifecycleEvents.ServerStarted callback : snapshot(SERVER_STARTED))
                callback.onServerStarted(server);
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
    }

    public static void onPlayerJoin(long handle) {
        if (!bootstrapped) return;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerPlayNetworkHandler network = new ServerPlayNetworkHandler(player);
        for (ServerPlayConnectionEvents.Join callback : snapshot(PLAYER_JOIN))
            invokeSafely(() -> callback.onPlayReady(network, PacketSender.NOOP, server), "player join");
    }

    public static void onPlayerQuit(long handle) {
        if (!bootstrapped) return;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        ServerPlayNetworkHandler network = new ServerPlayNetworkHandler(player);
        for (ServerPlayConnectionEvents.Disconnect callback : snapshot(PLAYER_QUIT))
            invokeSafely(() -> callback.onPlayDisconnect(network, server), "player quit");
        WrapperCache.remove(handle);
    }

    /** Return null to cancel, otherwise return the possibly rewritten message. */
    public static String onChat(long handle, String message) {
        if (!bootstrapped) return message;
        String current = message;
        ServerPlayerEntity player = ServerPlayerEntity.of(handle);
        for (ModEvents.Chat callback : ModEvents.CHAT.snapshot()) {
            final String input = current;
            current = callback.onChat(player, input);
            if (current == null) return null;
        }
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
            ActionResult result = callback.interact(player, world, Hand.MAIN_HAND,
                                                    new BlockHitResult(pos));
            if (result == ActionResult.FAIL || result == ActionResult.SUCCESS) return false;
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
            ActionResult result = callback.interact(player, world, Hand.MAIN_HAND,
                                                    new BlockHitResult(pos));
            if (result == ActionResult.FAIL || result == ActionResult.SUCCESS) return false;
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
            current = callback.onCommand(player, current);
            if (current == null) return null;
        }
        // Commands registered through Fabric's CommandRegistrationCallback
        // must be consumed here, otherwise the native command dispatcher
        // would run a second, unrelated command tree.
        if (server != null && server.getCommandManager().hasCommand(current)) {
            server.getCommandManager().execute(
                current, new net.minecraft.server.command.ServerCommandSource(player, server));
            return null;
        }
        return current;
    }

    public static boolean onEntityDamage(long playerHandle, long entityHandle,
                                         float amount, String cause) {
        if (!bootstrapped) return true;
        Entity victim = playerHandle != 0 ? ServerPlayerEntity.of(playerHandle) : Entity.of(entityHandle);
        if (victim == null) return true;
        for (ModEvents.EntityDamage callback : ModEvents.ENTITY_DAMAGE.snapshot())
            if (!callback.onEntityDamage(victim, amount, cause)) return false;
        return true;
    }

    public static boolean onMobSpawn(long entityHandle, double x, double y, double z) {
        if (!bootstrapped) return true;
        Entity entity = Entity.of(entityHandle);
        ServerWorld world = server == null ? null : server.getOverworld();
        for (ModEvents.MobSpawn callback : ModEvents.MOB_SPAWN.snapshot())
            if (!callback.onMobSpawn(entity, world, x, y, z)) return false;
        return true;
    }

    public static void registerServerStarted(ServerLifecycleEvents.ServerStarted callback) { synchronized (SERVER_STARTED) { SERVER_STARTED.add(callback); } }
    public static void registerServerStarting(ServerLifecycleEvents.ServerStarting callback) { synchronized (SERVER_STARTING) { SERVER_STARTING.add(callback); } }
    public static void registerServerStopping(ServerLifecycleEvents.ServerStopping callback) { synchronized (SERVER_STOPPING) { SERVER_STOPPING.add(callback); } }
    public static void registerServerStopped(ServerLifecycleEvents.ServerStopped callback) { synchronized (SERVER_STOPPED) { SERVER_STOPPED.add(callback); } }
    public static void registerTickStart(ServerTickEvents.Start callback) { synchronized (TICK_START) { TICK_START.add(callback); } }
    public static void registerTickEnd(ServerTickEvents.End callback) { synchronized (TICK_END) { TICK_END.add(callback); } }
    public static void registerWorldLoad(ServerWorldEvents.Load callback) { synchronized (WORLD_LOAD) { WORLD_LOAD.add(callback); } }
    public static void registerWorldUnload(ServerWorldEvents.Unload callback) { synchronized (WORLD_UNLOAD) { WORLD_UNLOAD.add(callback); } }
    public static void registerPlayerJoin(ServerPlayConnectionEvents.Join callback) { synchronized (PLAYER_JOIN) { PLAYER_JOIN.add(callback); } }
    public static void registerPlayerQuit(ServerPlayConnectionEvents.Disconnect callback) { synchronized (PLAYER_QUIT) { PLAYER_QUIT.add(callback); } }
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
        WORLD_LOAD.clear();
        WORLD_UNLOAD.clear();
        PLAYER_JOIN.clear();
        PLAYER_QUIT.clear();
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
        modLoader = new URLClassLoader(urls.toArray(URL[]::new), CppModRuntime.class.getClassLoader());
        int initializedEntrypoints = 0;
        registerMixins(order);
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
        if (modLoader == null) return;
        try { modLoader.close(); } catch (IOException e) { log("WARN", "mod classloader close failed: " + e); }
        modLoader = null;
    }

    private static void invokeSafely(Runnable action, String operation) {
        try { action.run(); } catch (Throwable failure) { log("ERROR", operation + " failed: " + failure); }
    }

    private static void log(String level, Object message) {
        try { NativeBridge.nativeLog(level, String.valueOf(message)); }
        catch (Throwable ignored) { System.err.println("[cppfm][jvm][" + level + "] " + message); }
    }

    private record Candidate(String id, Path path, List<String> dependencies,
                             List<String> entrypoints, List<String> mixinConfigs) {}
}
