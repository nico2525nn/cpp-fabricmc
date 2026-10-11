package cppfm.fixture;

import cppfm.bridge.NativeBridge;
import cppfm.api.ModEvents;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerWorldEvents;
import net.fabricmc.fabric.api.entity.event.v1.ServerEntityEvents;
import net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents;
import net.fabricmc.fabric.api.networking.v1.ServerPlayConnectionEvents;
import net.fabricmc.fabric.api.command.v2.CommandRegistrationCallback;
import net.fabricmc.fabric.api.resource.IdentifiableResourceReloadListener;
import net.fabricmc.fabric.api.resource.ResourceManagerHelper;
import net.minecraft.block.AbstractBlock;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.registry.Registry;
import net.minecraft.registry.Registries;
import net.minecraft.resource.ResourceManager;
import net.minecraft.resource.ResourceReloader;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.mob.HostileEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.entity.passive.AnimalEntity;
import net.minecraft.entity.passive.FishEntity;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.command.CommandManager;
import net.minecraft.server.command.ServerCommandSource;
import com.mojang.brigadier.arguments.IntegerArgumentType;
import net.minecraft.util.Identifier;
import net.minecraft.util.NativeAccess;
import net.minecraft.text.Text;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.profiler.Profiler;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

/** Deterministic integration fixture for the embedded loader and callbacks. */
public final class FixtureMod implements DedicatedServerModInitializer {
    private static final AtomicLong LAST_LOADED_PIG_HANDLE = new AtomicLong();
    private static final AtomicLong KILL_PROBE_PIG_HANDLE = new AtomicLong();
    private static final AtomicInteger KILL_PROBE_PIG_ID = new AtomicInteger(-1);
    private static final AtomicLong DISCARD_PROBE_PIG_HANDLE = new AtomicLong();
    private static final AtomicInteger DISCARD_PROBE_PIG_ID = new AtomicInteger(-1);
    private static final AtomicLong DAMAGE_PROBE_PIG_HANDLE = new AtomicLong();
    private static final AtomicLong DAMAGE_CANCEL_PLAYER_HANDLE = new AtomicLong();
    private static final AtomicInteger DAMAGE_CANCEL_PLAYER_ID = new AtomicInteger(-1);
    private static volatile float DAMAGE_CANCEL_PLAYER_HEALTH_BEFORE;
    private static final AtomicReference<String> SERVER_CALLBACK_THREAD =
        new AtomicReference<>();
    private static final AtomicInteger DAMAGE_CANCEL_CALLBACKS = new AtomicInteger();
    private static final AtomicInteger DAMAGE_NESTED_REJECTIONS = new AtomicInteger();
    private static final AtomicInteger DAMAGE_VETO_CALLBACKS = new AtomicInteger();
    private static final AtomicInteger DAMAGE_ALLOW_CALLBACKS = new AtomicInteger();
    private static final AtomicInteger DAMAGE_AFTER_VETO_CALLBACKS = new AtomicInteger();
    private static final AtomicInteger DAMAGE_AFTER_ALLOW_CALLBACKS = new AtomicInteger();
    private static final AtomicInteger DAMAGE_AFTER_DEATH_CALLBACKS = new AtomicInteger();
    private static final AtomicInteger SHUTDOWN_WORLD_UNLOAD_CALLBACKS = new AtomicInteger();
    private static final AtomicInteger SHUTDOWN_WORLD_UNLOAD_MASK = new AtomicInteger();
    private static final AtomicInteger SHUTDOWN_STOPPING_CALLBACKS = new AtomicInteger();
    private static final AtomicInteger SHUTDOWN_STOPPED_CALLBACKS = new AtomicInteger();
    private static volatile float DAMAGE_VETO_BASE;
    private static volatile float DAMAGE_VETO_TAKEN;
    private static volatile boolean DAMAGE_VETO_BLOCKED;
    private static volatile boolean DAMAGE_AFTER_DEATH_SAW_DEAD;

    private static void recordCallbackThread(String event) {
        String current = Thread.currentThread().getName();
        String baseline = SERVER_CALLBACK_THREAD.get();
        if (baseline == null && SERVER_CALLBACK_THREAD.compareAndSet(null, current)) {
            NativeBridge.nativeLog("INFO",
                "fixture CALLBACK_THREAD_BASELINE name=" + current);
            baseline = current;
        } else if (baseline == null) {
            baseline = SERVER_CALLBACK_THREAD.get();
        }
        if (baseline != null && !baseline.equals(current))
            NativeBridge.nativeLog("ERROR",
                "fixture CALLBACK_THREAD_MISMATCH event=" + event +
                " expected=" + baseline + " actual=" + current);
    }

    private static int probeEntityId(String message, String prefix) {
        if (!message.startsWith(prefix)) return -1;
        try {
            return Integer.parseInt(message.substring(prefix.length()));
        } catch (NumberFormatException ignored) {
            return -1;
        }
    }

    private static void recordShutdownCallback(String phase,
                                               AtomicInteger callbackCount) {
        int count = callbackCount.incrementAndGet();
        long nativeServer = NativeAccess.serverHandle();
        long worldHandle = NativeAccess.serverWorld(0);
        String worldName = NativeAccess.worldName(worldHandle);
        long tick = NativeAccess.currentTick();
        boolean stopped = "server-stopped".equals(phase);
        boolean nativeRead = nativeServer != 0L && tick > 0L &&
            (stopped
                ? worldHandle == 0L && serverWorldsClosed()
                : worldHandle != 0L && "minecraft:overworld".equals(worldName));
        boolean mutation = !"server-stopping".equals(phase) ||
            (NativeAccess.setWorldBlock(worldHandle, 1, -60, 1, 1) &&
             NativeAccess.worldBlock(worldHandle, 1, -60, 1) == 1);
        boolean ok = count == 1 && nativeRead && mutation;
        recordCallbackThread("shutdown_" + phase);
        System.out.println("fixture SHUTDOWN_EVENT status=" + (ok ? "PASS" : "FAIL") +
            " phase=" + phase + " count=" + count + " nativeServer=" + nativeServer +
            " worldHandle=" + worldHandle + " world=" + worldName + " tick=" + tick +
            " worldsClosed=" + (stopped && serverWorldsClosed() ? "PASS" : "N/A") +
            " nativeMutation=" + (mutation ? "PASS" : "FAIL"));
        System.out.flush();
    }

    private static boolean serverWorldsClosed() {
        net.minecraft.server.MinecraftServer server =
            net.minecraft.server.MinecraftServer.of(NativeAccess.serverHandle());
        return server != null && server.getWorlds().isEmpty() &&
            server.getOverworld() == null;
    }

    private static void recordWorldUnload(
            net.minecraft.server.MinecraftServer server, ServerWorld world) {
        int count = SHUTDOWN_WORLD_UNLOAD_CALLBACKS.incrementAndGet();
        String dimension = world == null ? "<null>" :
            world.getRegistryKey().getValue().toString();
        int bit = switch (dimension) {
            case "minecraft:overworld" -> 1;
            case "minecraft:the_nether" -> 2;
            case "minecraft:the_end" -> 4;
            default -> 0;
        };
        int priorMask = bit == 0 ? -1 : SHUTDOWN_WORLD_UNLOAD_MASK.getAndUpdate(mask -> mask | bit);
        long handle = world == null ? 0L : world.nativeHandle();
        long nativeServer = NativeAccess.serverHandle();
        long tick = NativeAccess.currentTick();
        boolean valid = server != null && world != null && bit != 0 &&
            (priorMask & bit) == 0 && nativeServer != 0L && handle != 0L &&
            dimension.equals(NativeAccess.worldName(handle)) && tick > 0L;
        recordCallbackThread("shutdown_world-unload");
        System.out.println("fixture SHUTDOWN_EVENT status=" +
            (valid && count <= 3 ? "PASS" : "FAIL") +
            " phase=world-unload count=" + count + " dimension=" + dimension +
            " nativeServer=" + nativeServer + " worldHandle=" + handle +
            " world=" + NativeAccess.worldName(handle) + " tick=" + tick);
        System.out.flush();
    }

    @Override
    public void onInitializeServer() {
        NativeBridge.nativeLog("INFO", "fixture entrypoint initialized");
        ResourceManagerHelper.SERVER_DATA.registerReloadListener(new IdentifiableResourceReloadListener() {
            @Override public Identifier getFabricId() {
                return Identifier.of("cppfm_fixture", "reload_success");
            }

            @Override
            public CompletableFuture<Void> reload(ResourceReloader.Synchronizer synchronizer,
                                                   ResourceManager manager,
                                                   Profiler prepareProfiler,
                                                   Profiler applyProfiler,
                                                   Executor prepareExecutor,
                                                   Executor applyExecutor) {
                boolean resourcePresent = manager.getResource(
                    Identifier.of("minecraft", "recipes/acacia_boat.json")).isPresent();
                NativeBridge.nativeLog("INFO", "fixture RELOAD_SUCCESS resource=" + resourcePresent);
                if (!resourcePresent)
                    throw new IllegalStateException("fixture resource lookup failed");
                return CompletableFuture.completedFuture(null);
            }
        });
        ResourceManagerHelper.SERVER_DATA.registerReloadListener(new IdentifiableResourceReloadListener() {
            @Override public Identifier getFabricId() {
                return Identifier.of("cppfm_fixture", "reload_failure");
            }

            @Override
            public CompletableFuture<Void> reload(ResourceReloader.Synchronizer synchronizer,
                                                   ResourceManager manager,
                                                   Profiler prepareProfiler,
                                                   Profiler applyProfiler,
                                                   Executor prepareExecutor,
                                                   Executor applyExecutor) {
                NativeBridge.nativeLog("INFO", "fixture RELOAD_FAILURE_INVOKED");
                throw new IllegalStateException("fixture reload failure");
            }
        });
        ResourceManagerHelper.SERVER_DATA.registerReloadListener(new IdentifiableResourceReloadListener() {
            @Override public Identifier getFabricId() {
                return Identifier.of("cppfm_fixture", "reload_after_failure");
            }

            @Override
            public CompletableFuture<Void> reload(ResourceReloader.Synchronizer synchronizer,
                                                   ResourceManager manager,
                                                   Profiler prepareProfiler,
                                                   Profiler applyProfiler,
                                                   Executor prepareExecutor,
                                                   Executor applyExecutor) {
                NativeBridge.nativeLog("INFO", "fixture RELOAD_AFTER_FAILURE");
                return CompletableFuture.completedFuture(null);
            }
        });
        Block registered = Registry.register(Registries.BLOCK,
            Identifier.of("cppfm_fixture", "probe"),
            new Block(AbstractBlock.Settings.create()));
        ServerLifecycleEvents.SERVER_STARTED.register(server -> {
            recordCallbackThread("server_started");
            ServerWorld world = server.getOverworld();
            BlockPos pos = new BlockPos(0, -60, 0);
            BlockState before = world.getBlockState(pos);
            world.setBlockState(pos, before);
            NativeBridge.nativeLog("INFO", "fixture WORLD_API " + registered.getId());
            java.util.List<ServerWorld> worlds = server.getWorlds();
            boolean dimensionsMatch = worlds.size() == 3 &&
                "minecraft:overworld".equals(
                    worlds.get(0).getRegistryKey().getValue().toString()) &&
                worlds.get(0).getDimension().minY() == -64 &&
                worlds.get(0).getDimension().height() == 384 &&
                worlds.get(0).getBottomY() == -64 &&
                "minecraft:the_nether".equals(
                    worlds.get(1).getRegistryKey().getValue().toString()) &&
                worlds.get(1).getDimension().minY() == 0 &&
                worlds.get(1).getDimension().height() == 256 &&
                worlds.get(1).getBottomY() == 0 &&
                "minecraft:the_end".equals(
                    worlds.get(2).getRegistryKey().getValue().toString()) &&
                worlds.get(2).getDimension().minY() == 0 &&
                worlds.get(2).getDimension().height() == 256 &&
                worlds.get(2).getBottomY() == 0;
            NativeBridge.nativeLog("INFO",
                "fixture WORLD_DIMENSION_SURFACES " + dimensionsMatch);
        });
        ServerLifecycleEvents.SERVER_STARTED.register(server ->
            NativeBridge.nativeLog("INFO", "fixture SERVER_STARTED"));
        ServerWorldEvents.UNLOAD.register(FixtureMod::recordWorldUnload);
        ServerLifecycleEvents.SERVER_STOPPING.register(server ->
            recordShutdownCallback("server-stopping",
                SHUTDOWN_STOPPING_CALLBACKS));
        ServerLifecycleEvents.SERVER_STOPPED.register(server ->
            recordShutdownCallback("server-stopped",
                SHUTDOWN_STOPPED_CALLBACKS));
        ServerTickEvents.END.register(server -> {
            recordCallbackThread("server_tick");
            long canceledPlayerHandle = DAMAGE_CANCEL_PLAYER_HANDLE.getAndSet(0L);
            if (canceledPlayerHandle != 0L) {
                float after = NativeAccess.entityHealth(canceledPlayerHandle);
                NativeBridge.nativeLog("INFO",
                    "fixture DAMAGE_CANCEL_NATIVE_HEALTH id=" +
                    DAMAGE_CANCEL_PLAYER_ID.get() + " before=" +
                    DAMAGE_CANCEL_PLAYER_HEALTH_BEFORE + " after=" + after);
            }
            if (server.getTicks() == 1)
                NativeBridge.nativeLog("INFO", "fixture END_SERVER_TICK");
            if (server.getTicks() == 1) {
                NativeBridge.nativeExecuteCommand("cppfm_probe 8");
                boolean reloadResult = NativeBridge.nativeExecuteCommand("reload");
                NativeBridge.nativeLog("INFO", "fixture RELOAD_COMMAND_RESULT " + reloadResult);
            }
            if (server.getTicks() == 2)
                NativeBridge.nativeLog("INFO", "fixture POST_RELOAD_TICK");
            if (server.getTicks() == 3) {
                boolean summoned = NativeBridge.nativeExecuteCommand(
                    "summon minecraft:pig");
                long handle = LAST_LOADED_PIG_HANDLE.get();
                DAMAGE_PROBE_PIG_HANDLE.set(handle);
                net.minecraft.entity.LivingEntity victim =
                    net.minecraft.entity.LivingEntity.of(handle);
                boolean setup = summoned && handle != 0L && victim != null &&
                    victim.getMaxHealth() == 10.0f &&
                    NativeAccess.setEntityHealth(handle, 4.0f);
                if (setup) {
                    boolean cancelDamageResult = victim.damage(
                        new net.minecraft.entity.damage.DamageSource(
                            "fixture_damage_cancel"), 1.0f);
                    float cancelHealth = NativeAccess.entityHealth(handle);
                    int cancelCallbacks = DAMAGE_CANCEL_CALLBACKS.get();
                    boolean cancelled = !cancelDamageResult &&
                        cancelHealth == 4.0f && cancelCallbacks == 1;
                    NativeBridge.nativeLog("INFO",
                        "fixture DAMAGE_PROBE_CANCEL result=" +
                        cancelDamageResult + " health=" + cancelHealth +
                        " callbacks=" + cancelCallbacks);
                    NativeBridge.nativeLog("INFO",
                        "fixture DAMAGE_BRIDGE_CANCEL " + cancelled);

                    boolean outerDamage = victim.damage(
                        new net.minecraft.entity.damage.DamageSource(
                            "fixture_nested_guard"), 1.0f);
                    boolean nestedRejected = outerDamage &&
                        DAMAGE_NESTED_REJECTIONS.get() == 1 &&
                        NativeAccess.entityHealth(handle) == 3.0f;
                    NativeBridge.nativeLog("INFO",
                        "fixture DAMAGE_BRIDGE_NESTED_REJECT " + nestedRejected);

                    NativeAccess.setEntityHealth(handle, 4.0f);
                    boolean vetoCall = victim.damage(
                        new net.minecraft.entity.damage.DamageSource(
                            "fixture_death_veto"), 100.0f);
                    boolean deathVeto = vetoCall && !victim.isDead() &&
                        NativeAccess.entityHealth(handle) == 4.0f &&
                        DAMAGE_VETO_CALLBACKS.get() == 1 &&
                        DAMAGE_AFTER_VETO_CALLBACKS.get() == 1 &&
                        DAMAGE_VETO_BASE == 100.0f &&
                        DAMAGE_VETO_TAKEN == 100.0f &&
                        !DAMAGE_VETO_BLOCKED &&
                        DAMAGE_AFTER_DEATH_CALLBACKS.get() == 0;
                    NativeBridge.nativeLog("INFO",
                        "fixture DAMAGE_BRIDGE_DEATH_VETO " + deathVeto);

                    boolean allowedCall = victim.damage(
                        new net.minecraft.entity.damage.DamageSource(
                            "fixture_death_allow"), 100.0f);
                    boolean deathAllowed = allowedCall && victim.isDead() &&
                        DAMAGE_ALLOW_CALLBACKS.get() == 1 &&
                        DAMAGE_AFTER_ALLOW_CALLBACKS.get() == 0 &&
                        DAMAGE_AFTER_DEATH_CALLBACKS.get() == 1 &&
                        DAMAGE_AFTER_DEATH_SAW_DEAD;
                    NativeBridge.nativeLog("INFO",
                        "fixture DAMAGE_BRIDGE_DEATH_ALLOW " + deathAllowed);
                }
                NativeBridge.nativeLog("INFO",
                    "fixture DAMAGE_BRIDGE_SETUP " + setup);
            }
            if (server.getTicks() == 1 && server.getTickTime() == 42L)
                NativeBridge.nativeLog("INFO", "fixture MIXIN_OVERWRITE tick=" + server.getTicks());
        });
        CommandRegistrationCallback.EVENT.register((dispatcher, registry, environment) -> {
            dispatcher.register(CommandManager.<ServerCommandSource>literal("cppfm_probe")
                .then(CommandManager.<ServerCommandSource, Integer>argument(
                    "value", IntegerArgumentType.integer(1, 9))
                    .executes(context -> {
                        NativeBridge.nativeLog("INFO", "fixture COMMAND_EXECUTED " +
                            IntegerArgumentType.getInteger(context, "value"));
                        return 1;
                    })));
            NativeBridge.nativeLog("INFO", "fixture COMMAND_REGISTERED");
        });
        ServerLifecycleEvents.SERVER_STARTED.register(server ->
            server.getCommandManager().execute("cppfm_probe 7",
                new ServerCommandSource(null, server)));
        ModEvents.BLOCK_CLICKED.register((player, world, pos, state, face) -> {
            boolean executed = NativeBridge.nativeExecuteCommand("time set day");
            NativeBridge.nativeLog("INFO",
                "fixture BLOCK_CLICK_NATIVE_RESULT " + executed);
            return true;
        });
        ModEvents.BLOCK_PLACE.register((player, world, pos, state) -> {
            boolean executed = NativeBridge.nativeExecuteCommand("time set day");
            NativeBridge.nativeLog("INFO",
                "fixture BLOCK_PLACE_NATIVE_RESULT " + executed);
            return true;
        });
        ModEvents.BLOCK_BREAK.register((player, world, pos, state) -> {
            boolean executed = NativeBridge.nativeExecuteCommand("time set day");
            NativeBridge.nativeLog("INFO",
                "fixture BLOCK_BREAK_NATIVE_RESULT " + executed);
            return true;
        });
        ServerEntityEvents.ENTITY_LOAD.register((entity, world) -> {
            recordCallbackThread("entity_load");
            String type = entity.getType().getId().toString();
            if ("JvmJoinKick".equals(entity.getName().getString()))
                NativeBridge.nativeLog("INFO", "fixture JOIN_KICK_ENTITY_LOAD");
            NativeBridge.nativeLog("INFO", "fixture ENTITY_CLASS type=" + type +
                " hostile=" + (entity instanceof HostileEntity) +
                " animal=" + (entity instanceof AnimalEntity) +
                " fish=" + (entity instanceof FishEntity) +
                " maxHealth=" + (entity instanceof LivingEntity living
                    ? living.getMaxHealth() : -1.0f));
            if (!"minecraft:pig".equals(type)) return;
            LAST_LOADED_PIG_HANDLE.set(entity.nativeHandle());
            NativeBridge.nativeLog("INFO", "fixture ENTITY_LOAD_MOB_WRAPPER " +
                (entity instanceof MobEntity));
            boolean present = world.getEntityById(entity.getId()) == entity;
            NativeBridge.nativeLog("INFO",
                "fixture ENTITY_LOAD_PRESENT " + present +
                " dimension=" + world.getRegistryKey().getValue());
            NativeBridge.nativeLog("INFO", "fixture ENTITY_LOAD_ID id=" +
                entity.getId() + " dimension=" +
                world.getRegistryKey().getValue());
        });
        ServerEntityEvents.ENTITY_UNLOAD.register((entity, world) -> {
            recordCallbackThread("entity_unload");
            if ("JvmJoinKick".equals(entity.getName().getString()))
                NativeBridge.nativeLog("INFO", "fixture JOIN_KICK_ENTITY_UNLOAD");
            if (!"minecraft:pig".equals(entity.getType().getId().toString())) return;
            NativeBridge.nativeLog("INFO", "fixture ENTITY_UNLOAD_MOB_WRAPPER " +
                (entity instanceof MobEntity));
            boolean present = world.getEntityById(entity.getId()) == entity;
            NativeBridge.nativeLog("INFO",
                "fixture ENTITY_UNLOAD_PRESENT " + present +
                " dimension=" + world.getRegistryKey().getValue());
            NativeBridge.nativeLog("INFO", "fixture ENTITY_UNLOAD_ID id=" +
                entity.getId() + " present=" + present + " dimension=" +
                world.getRegistryKey().getValue());
            if (entity.getId() == KILL_PROBE_PIG_ID.get()) {
                NativeBridge.nativeLog("INFO",
                    "fixture ENTITY_KILL_UNLOAD_TARGET id=" +
                    entity.getId() + " present=" + present);
            }
            if (entity.getId() == DISCARD_PROBE_PIG_ID.get()) {
                NativeBridge.nativeLog("INFO",
                    "fixture ENTITY_DISCARD_UNLOAD_TARGET true");
                NativeBridge.nativeLog("INFO",
                    "fixture ENTITY_DISCARD_UNLOAD_PRESENT " + present);
            }
        });
        ServerLivingEntityEvents.ALLOW_DAMAGE.register((entity, source, amount) -> {
            if (!(entity instanceof ServerPlayerEntity) ||
                !"JvmReenterB".equals(entity.getName().getString())) return true;
            recordCallbackThread("allow_damage");
            DAMAGE_CANCEL_PLAYER_ID.set(entity.getId());
            DAMAGE_CANCEL_PLAYER_HEALTH_BEFORE =
                NativeAccess.entityHealth(entity.nativeHandle());
            DAMAGE_CANCEL_PLAYER_HANDLE.set(entity.nativeHandle());
            NativeBridge.nativeLog("INFO", "fixture DAMAGE_CANCEL_VICTIM id=" +
                entity.getId() + " nativeHealth=" +
                DAMAGE_CANCEL_PLAYER_HEALTH_BEFORE);
            return false;
        });
        ServerLivingEntityEvents.ALLOW_DAMAGE.register((entity, source, amount) -> {
            if (entity.nativeHandle() != DAMAGE_PROBE_PIG_HANDLE.get()) return true;
            if ("fixture_damage_cancel".equals(source.getName())) {
                DAMAGE_CANCEL_CALLBACKS.incrementAndGet();
                return false;
            }
            if ("fixture_nested_guard".equals(source.getName())) {
                boolean nested = entity.damage(
                    new net.minecraft.entity.damage.DamageSource(
                        "fixture_nested_guard"), 1.0f);
                if (!nested) DAMAGE_NESTED_REJECTIONS.incrementAndGet();
            }
            return true;
        });
        ServerLivingEntityEvents.ALLOW_DEATH.register((entity, source, amount) -> {
            if (entity.nativeHandle() != DAMAGE_PROBE_PIG_HANDLE.get()) return true;
            if ("fixture_death_veto".equals(source.getName())) {
                DAMAGE_VETO_CALLBACKS.incrementAndGet();
                // Fabric cancellation does not restore health: the listener
                // must explicitly put the entity back above zero.
                entity.setHealth(4.0f);
                return false;
            }
            if ("fixture_death_allow".equals(source.getName())) {
                DAMAGE_ALLOW_CALLBACKS.incrementAndGet();
            }
            return true;
        });
        ServerLivingEntityEvents.AFTER_DAMAGE.register(
            (entity, source, baseDamage, damageTaken, blocked) -> {
                if (entity instanceof ServerPlayerEntity &&
                    "JvmReenterB".equals(entity.getName().getString())) {
                    NativeBridge.nativeLog("INFO",
                        "fixture DAMAGE_CANCEL_AFTER_APPLIED " +
                        source.getName());
                }
                if (entity.nativeHandle() != DAMAGE_PROBE_PIG_HANDLE.get()) return;
                if ("fixture_death_veto".equals(source.getName())) {
                    DAMAGE_AFTER_VETO_CALLBACKS.incrementAndGet();
                    DAMAGE_VETO_BASE = baseDamage;
                    DAMAGE_VETO_TAKEN = damageTaken;
                    DAMAGE_VETO_BLOCKED = blocked;
                }
                if ("fixture_death_allow".equals(source.getName()))
                    DAMAGE_AFTER_ALLOW_CALLBACKS.incrementAndGet();
            });
        ServerLivingEntityEvents.AFTER_DEATH.register((entity, source) -> {
            if (entity.nativeHandle() != DAMAGE_PROBE_PIG_HANDLE.get()) return;
            if ("fixture_death_allow".equals(source.getName())) {
                DAMAGE_AFTER_DEATH_CALLBACKS.incrementAndGet();
                DAMAGE_AFTER_DEATH_SAW_DEAD = entity.isDead();
            }
        });
        ServerPlayConnectionEvents.JOIN.register((handler, sender, server) -> {
            recordCallbackThread("player_join");
            if (!"JvmJoinKick".equals(handler.getPlayer().getName().getString())) return;
            NativeBridge.nativeLog("INFO", "fixture JOIN_DISCONNECT_REQUESTED");
            handler.disconnect(Text.literal("fixture rejected join"));
        });
        ServerPlayConnectionEvents.DISCONNECT.register((handler, server) -> {
            recordCallbackThread("player_disconnect");
            if ("JvmJoinKick".equals(handler.getPlayer().getName().getString()))
                NativeBridge.nativeLog("INFO", "fixture JOIN_KICK_DISCONNECTED");
        });
        ModEvents.CHAT.register((player, message) -> {
            recordCallbackThread("chat");
            if (message.startsWith("cppfm_kill_probe:")) {
                int entityId = probeEntityId(message, "cppfm_kill_probe:");
                ServerPlayerEntity serverPlayer = (ServerPlayerEntity) player;
                Entity entity = entityId < 0 ? null
                    : serverPlayer.getServerWorld().getEntityById(entityId);
                if (entity != null &&
                    !"minecraft:pig".equals(
                        entity.getType().getId().toString()))
                    entity = null;
                long handle = entity == null ? 0L : entity.nativeHandle();
                KILL_PROBE_PIG_HANDLE.set(handle);
                KILL_PROBE_PIG_ID.set(entityId);
                if (entity != null)
                    entity.remove(Entity.RemovalReason.KILLED);
                NativeBridge.nativeLog("INFO", "fixture ENTITY_KILL_REQUEST id=" +
                    entityId + " removed=" +
                    (entity != null && entity.isRemoved()));
            }
            if ("cppfm_kill_verify".equals(message)) {
                long handle = KILL_PROBE_PIG_HANDLE.get();
                int entityId = KILL_PROBE_PIG_ID.get();
                NativeBridge.nativeLog("INFO", "fixture ENTITY_KILL_VERIFY id=" +
                    entityId + " absent=" +
                    nativeEntityHandleIsAbsent(handle, entityId));
            }
            if (message.startsWith("cppfm_discard_probe:")) {
                int entityId = probeEntityId(message, "cppfm_discard_probe:");
                ServerPlayerEntity serverPlayer = (ServerPlayerEntity) player;
                Entity entity = entityId < 0 ? null
                    : serverPlayer.getServerWorld().getEntityById(entityId);
                if (entity != null &&
                    !"minecraft:pig".equals(
                        entity.getType().getId().toString()))
                    entity = null;
                long handle = entity == null ? 0L : entity.nativeHandle();
                DISCARD_PROBE_PIG_HANDLE.set(handle);
                DISCARD_PROBE_PIG_ID.set(entityId);
                if (entity != null) entity.discard();
                NativeBridge.nativeLog("INFO", "fixture ENTITY_DISCARD_REQUEST " +
                    (entity != null && entity.isRemoved()));
                NativeBridge.nativeLog("INFO",
                    "fixture ENTITY_DISCARD_REQUEST_STATE id=" + entityId +
                    " handle=" + handle);
            }
            if ("cppfm_discard_verify".equals(message)) {
                long handle = DISCARD_PROBE_PIG_HANDLE.get();
                boolean staleHandle = "minecraft:unknown".equals(
                    NativeAccess.entityType(handle));
                boolean presentInRoster = false;
                final int targetEntityId = DISCARD_PROBE_PIG_ID.get();
                final int entityCount = (int) NativeAccess.entityCount();
                int matchingRosterEntries = 0;
                for (int index = 0; index < entityCount; ++index) {
                    long currentHandle = NativeAccess.entityHandle(index);
                    if (currentHandle != 0 &&
                        NativeAccess.playerEntityId(currentHandle) == targetEntityId) {
                        presentInRoster = true;
                        matchingRosterEntries++;
                        break;
                    }
                }
                NativeBridge.nativeLog("INFO", "fixture ENTITY_DISCARD_VERIFY " +
                    (staleHandle && !presentInRoster));
                NativeBridge.nativeLog("INFO", "fixture ENTITY_DISCARD_VERIFY_STATE " +
                    "stale=" + staleHandle + " present=" + presentInRoster +
                    " id=" + targetEntityId + " handle=" + handle +
                    " count=" + entityCount + " matches=" + matchingRosterEntries);
            }
            if ("cppfm_slow_chat_probe".equals(message)) {
                NativeBridge.nativeLog("INFO", "fixture CHAT_CALLBACK_SLOW_ENTER");
                try {
                    Thread.sleep(1500L);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                }
                NativeBridge.nativeLog("INFO", "fixture CHAT_CALLBACK_SLOW_EXIT");
            }
            return message;
        });
        ModEvents.COMMAND.register((player, command) -> {
            recordCallbackThread("command");
            if ("cppfm_parallel_probe".equals(command)) {
                boolean executed = NativeBridge.nativeExecuteCommand("time set day");
                NativeBridge.nativeLog("INFO",
                    "fixture PARALLEL_COMMAND_NATIVE_RESULT " + executed);
                return null;
            }
            if (!"cppfm_callback_probe".equals(command)) return command;
            boolean executed = NativeBridge.nativeExecuteCommand("time set day");
            NativeBridge.nativeLog("INFO",
                "fixture PLAYER_COMMAND_NATIVE_RESULT " + executed);
            return null;
        });
    }

    private static boolean nativeEntityHandleIsAbsent(long handle, int entityId) {
        if (handle == 0L || entityId < 0 ||
            !"minecraft:unknown".equals(NativeAccess.entityType(handle)))
            return false;
        int count = (int) Math.min(Integer.MAX_VALUE, NativeAccess.entityCount());
        if (count == 0) return false;
        for (int index = 0; index < count; index++) {
            long currentHandle = NativeAccess.entityHandle(index);
            if (currentHandle != 0L &&
                NativeAccess.playerEntityId(currentHandle) == entityId)
                return false;
        }
        return true;
    }
}
