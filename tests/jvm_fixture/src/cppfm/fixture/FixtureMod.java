package cppfm.fixture;

import cppfm.bridge.NativeBridge;
import cppfm.api.ModEvents;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerWorldEvents;
import net.fabricmc.fabric.api.entity.event.v1.ServerEntityEvents;
import net.fabricmc.fabric.api.entity.event.v1.ServerLivingEntityEvents;
import net.fabricmc.fabric.api.entity.event.v1.ServerEntityWorldChangeEvents;
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
import com.mojang.brigadier.arguments.StringArgumentType;
import net.minecraft.util.Identifier;
import net.minecraft.util.NativeAccess;
import net.minecraft.text.Text;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.TeleportTarget;
import net.minecraft.world.World;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;
import java.lang.reflect.Field;

/** Deterministic integration fixture for the embedded loader and callbacks. */
public final class FixtureMod implements DedicatedServerModInitializer {
    private static final AtomicLong LAST_LOADED_PIG_HANDLE = new AtomicLong();
    private static final AtomicLong KILL_PROBE_PIG_HANDLE = new AtomicLong();
    private static final AtomicInteger KILL_PROBE_PIG_ID = new AtomicInteger(-1);
    private static final AtomicLong DISCARD_PROBE_PIG_HANDLE = new AtomicLong();
    private static final AtomicInteger DISCARD_PROBE_PIG_ID = new AtomicInteger(-1);
    private static final AtomicLong DAMAGE_PROBE_PIG_HANDLE = new AtomicLong();
    private static final AtomicLong NATIVE_QUIT_PLAYER_HANDLE = new AtomicLong();
    private static final AtomicInteger NATIVE_QUIT_RECHECK_TICKS = new AtomicInteger();
    private static final AtomicInteger NATIVE_QUIT_INIT_COUNT = new AtomicInteger();
    private static final AtomicLong DAMAGE_CANCEL_PLAYER_HANDLE = new AtomicLong();
    private static final AtomicInteger DAMAGE_CANCEL_PLAYER_ID = new AtomicInteger(-1);
    private static final AtomicInteger PLAYER_WORLD_CHANGE_EVENT_COUNT =
        new AtomicInteger();
    private static final AtomicReference<String> LAST_PLAYER_WORLD_CHANGE_EVENT =
        new AtomicReference<>("");
    private static final AtomicReference<ServerPlayerEntity>
        LAST_PLAYER_WORLD_CHANGE_PLAYER = new AtomicReference<>();
    private static final AtomicReference<ServerWorld>
        LAST_PLAYER_WORLD_CHANGE_ORIGIN = new AtomicReference<>();
    private static final AtomicReference<ServerWorld>
        LAST_PLAYER_WORLD_CHANGE_DESTINATION = new AtomicReference<>();
    private static final AtomicReference<ServerPlayerEntity>
        NATIVE_PORTAL_EXPECTED_PLAYER = new AtomicReference<>();
    private static final AtomicReference<ServerWorld>
        NATIVE_PORTAL_EXPECTED_ORIGIN = new AtomicReference<>();
    private static final AtomicReference<ServerWorld>
        NATIVE_PORTAL_EXPECTED_DESTINATION = new AtomicReference<>();
    private static final AtomicInteger NATIVE_PORTAL_EXPECTED_EVENT_COUNT =
        new AtomicInteger(-1);
    private static volatile float DAMAGE_CANCEL_PLAYER_HEALTH_BEFORE;
    private static final AtomicReference<String> SERVER_CALLBACK_THREAD =
        new AtomicReference<>();
    private static final AtomicInteger DAMAGE_CANCEL_CALLBACKS = new AtomicInteger();
    private static final AtomicInteger DAMAGE_NESTED_REJECTIONS = new AtomicInteger();
    private static final AtomicInteger DAMAGE_VETO_CALLBACKS = new AtomicInteger();
    private static final AtomicInteger DAMAGE_UNHEALED_VETO_CALLBACKS =
        new AtomicInteger();
    private static final AtomicInteger DAMAGE_ALLOW_CALLBACKS = new AtomicInteger();
    private static final AtomicBoolean PLAYER_ROTATION_PROBE_REPORTED =
        new AtomicBoolean();
    private static final AtomicBoolean WORKER_ENTITY_QUERY_PROBE_REPORTED =
        new AtomicBoolean();
    private static final AtomicBoolean WORKER_ENTITY_RETIREMENT_STARTED =
        new AtomicBoolean();
    private static final AtomicBoolean WORKER_ENTITY_RETIREMENT_REPORTED =
        new AtomicBoolean();
    private static final AtomicBoolean WORKER_ENTITY_RETIREMENT_STOP =
        new AtomicBoolean();
    private static final AtomicBoolean WORKER_ENTITY_RETIREMENT_SAW_TARGET =
        new AtomicBoolean();
    private static final AtomicInteger WORKER_ENTITY_RETIREMENT_READS =
        new AtomicInteger();
    private static final AtomicInteger WORKER_ENTITY_RETIREMENT_TARGET_ID =
        new AtomicInteger(-1);
    private static final AtomicReference<String> WORKER_ENTITY_RETIREMENT_FAILURE =
        new AtomicReference<>("");
    private static final AtomicReference<Boolean> WORKER_ENTITY_RETIREMENT_RESULT =
        new AtomicReference<>();
    private static final AtomicReference<String> WORKER_ENTITY_RETIREMENT_HANDLE_STATE =
        new AtomicReference<>("not-requested");
    private static final AtomicReference<Thread> WORKER_ENTITY_READER =
        new AtomicReference<>();
    private static final AtomicInteger DAMAGE_AFTER_VETO_CALLBACKS = new AtomicInteger();
    private static final AtomicInteger DAMAGE_AFTER_UNHEALED_VETO_CALLBACKS =
        new AtomicInteger();
    private static final AtomicInteger DAMAGE_AFTER_UNHEALED_DEATH_CALLBACKS =
        new AtomicInteger();
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
        boolean mutation = true;
        if ("server-stopping".equals(phase)) {
            final int blockX = 1;
            final int blockY = -60;
            final int blockZ = 1;
            int currentState = NativeAccess.worldBlock(
                worldHandle, blockX, blockY, blockZ);
            int replacementState = currentState == 1 ? 0 : 1;
            mutation = NativeAccess.setWorldBlock(
                    worldHandle, blockX, blockY, blockZ, replacementState) &&
                NativeAccess.worldBlock(
                    worldHandle, blockX, blockY, blockZ) == replacementState;
        }
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

    private static void verifyWorkerEntityQuery(ServerWorld world) {
        AtomicReference<String> result = new AtomicReference<>();
        Thread worker = new Thread(() -> {
            try {
                result.set(Integer.toString(world.getEntities().size()));
            } catch (Throwable failure) {
                result.set("error:" + failure.getClass().getName());
            }
        }, "cppfm-fixture-entity-query");
        worker.setDaemon(true);
        worker.start();

        try {
            worker.join(3000);
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
        }
        String entities = result.get();
        boolean passed = !worker.isAlive() && entities != null &&
            !entities.startsWith("error:");
        NativeBridge.nativeLog("INFO",
            "fixture CALLBACK_WORKER_ENTITY_QUERY status=" +
            (passed ? "PASS" : "FAIL") + " entities=" +
            (entities == null ? "timeout" : entities));
    }

    private static void startWorkerEntityRetirementProbe(
            net.minecraft.server.MinecraftServer server) {
        if (!WORKER_ENTITY_RETIREMENT_STARTED.compareAndSet(false, true)) return;
        ServerWorld world = server.getOverworld();
        // Keep the probe clear of the flat-world floor/void. The retirement
        // race must be initiated by the worker request, not by suffocation or
        // another environmental death during a slow CI tick.
        boolean summoned = world != null && NativeBridge.nativeExecuteCommand(
            "summon minecraft:pig 4 100 4");
        long handle = LAST_LOADED_PIG_HANDLE.get();
        Entity target = Entity.of(handle);
        if (!summoned || target == null || handle == 0L) {
            WORKER_ENTITY_RETIREMENT_FAILURE.set("fixture-entity-unavailable");
            return;
        }
        WORKER_ENTITY_RETIREMENT_TARGET_ID.set(target.getId());

        CountDownLatch firstReadComplete = new CountDownLatch(1);
        Thread reader = new Thread(() -> {
            try {
                for (Entity entity : world.getEntities()) {
                    if (entity.getId() == WORKER_ENTITY_RETIREMENT_TARGET_ID.get())
                        WORKER_ENTITY_RETIREMENT_SAW_TARGET.set(true);
                    entity.getType().getId().toString();
                }
                WORKER_ENTITY_RETIREMENT_READS.incrementAndGet();
            } catch (Throwable failure) {
                WORKER_ENTITY_RETIREMENT_FAILURE.compareAndSet(
                    "", "initial-read:" + failure.getClass().getName());
            } finally {
                firstReadComplete.countDown();
            }
            while (!WORKER_ENTITY_RETIREMENT_STOP.get()) {
                try {
                    for (Entity entity : world.getEntities()) {
                        entity.getId();
                        entity.getType().getId().toString();
                    }
                    WORKER_ENTITY_RETIREMENT_READS.incrementAndGet();
                    Thread.yield();
                } catch (Throwable failure) {
                    WORKER_ENTITY_RETIREMENT_FAILURE.compareAndSet(
                        "", "concurrent-read:" + failure.getClass().getName());
                    break;
                }
            }
        }, "cppfm-fixture-entity-reader");
        reader.setDaemon(true);

        WORKER_ENTITY_READER.set(reader);
        reader.start();
        try {
            if (!firstReadComplete.await(2, TimeUnit.SECONDS)) {
                WORKER_ENTITY_RETIREMENT_FAILURE.compareAndSet(
                    "", "server-callback-worker-read-blocked");
            } else if (!WORKER_ENTITY_RETIREMENT_SAW_TARGET.get()) {
                WORKER_ENTITY_RETIREMENT_FAILURE.compareAndSet(
                    "", "target-not-in-initial-snapshot");
            } else {
                // Keep the actual mutation on the server owner thread. The
                // reader remains concurrent while the next native tick closes
                // publication, invalidates the handle, and removes the roster
                // entry; a second queued writer here would only test its
                // bounded admission deadline, not retirement synchronization.
                WORKER_ENTITY_RETIREMENT_HANDLE_STATE.set(
                    "before=" + NativeAccess.entityType(handle) + "/" +
                    NativeAccess.playerEntityId(handle) + "/dead=" +
                    NativeAccess.entityDead(handle));
                WORKER_ENTITY_RETIREMENT_RESULT.set(
                    NativeAccess.removeEntity(handle, 1));
                WORKER_ENTITY_RETIREMENT_HANDLE_STATE.updateAndGet(before ->
                    before + ",after=" + NativeAccess.entityType(handle) + "/" +
                    NativeAccess.playerEntityId(handle));
            }
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            WORKER_ENTITY_RETIREMENT_FAILURE.compareAndSet(
                "", "server-callback-interrupted");
        }
    }

    private static void finishWorkerEntityRetirementProbe(ServerWorld world,
                                                           long tick) {
        if (!WORKER_ENTITY_RETIREMENT_STARTED.get() ||
                WORKER_ENTITY_RETIREMENT_REPORTED.get()) return;
        int targetId = WORKER_ENTITY_RETIREMENT_TARGET_ID.get();
        boolean absent = targetId >= 0 && world.getEntityById(targetId) == null;
        if (!absent && tick < 7) return;

        WORKER_ENTITY_RETIREMENT_STOP.set(true);
        Thread reader = WORKER_ENTITY_READER.get();
        try {
            if (reader != null) reader.join(1000);
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            WORKER_ENTITY_RETIREMENT_FAILURE.compareAndSet(
                "", "join-interrupted");
        }
        boolean passed = absent && WORKER_ENTITY_RETIREMENT_SAW_TARGET.get() &&
            WORKER_ENTITY_RETIREMENT_READS.get() > 0 &&
            Boolean.TRUE.equals(WORKER_ENTITY_RETIREMENT_RESULT.get()) &&
            WORKER_ENTITY_RETIREMENT_FAILURE.get().isEmpty() &&
            reader != null && !reader.isAlive();
        WORKER_ENTITY_RETIREMENT_REPORTED.set(true);
        NativeBridge.nativeLog("INFO",
            "fixture CALLBACK_WORKER_ENTITY_RETIREMENT status=" +
            (passed ? "PASS" : "FAIL") + " reads=" +
            WORKER_ENTITY_RETIREMENT_READS.get() + " removed=" + absent +
            " sawTarget=" + WORKER_ENTITY_RETIREMENT_SAW_TARGET.get() +
            " requestAccepted=" + WORKER_ENTITY_RETIREMENT_RESULT.get() +
            " readerAlive=" + (reader != null && reader.isAlive()) +
            " targetId=" + targetId +
            " handleState=" + WORKER_ENTITY_RETIREMENT_HANDLE_STATE.get() +
            " error=" + WORKER_ENTITY_RETIREMENT_FAILURE.get());
    }

    private record EntityShadow(World world, double x, double y, double z,
                                float yaw, float pitch) { }

    private static EntityShadow captureEntityShadow(Entity entity)
            throws ReflectiveOperationException {
        Class<?> type = Entity.class;
        Field world = type.getDeclaredField("world");
        Field x = type.getDeclaredField("localX");
        Field y = type.getDeclaredField("localY");
        Field z = type.getDeclaredField("localZ");
        Field yaw = type.getDeclaredField("yaw");
        Field pitch = type.getDeclaredField("pitch");
        for (Field field : new Field[] {world, x, y, z, yaw, pitch})
            field.setAccessible(true);
        return new EntityShadow((World) world.get(entity), x.getDouble(entity),
            y.getDouble(entity), z.getDouble(entity), yaw.getFloat(entity),
            pitch.getFloat(entity));
    }

    private static boolean entityShadowEquals(Entity entity,
                                               EntityShadow expected) {
        try {
            EntityShadow actual = captureEntityShadow(entity);
            return actual.world() == expected.world() &&
                Double.compare(actual.x(), expected.x()) == 0 &&
                Double.compare(actual.y(), expected.y()) == 0 &&
                Double.compare(actual.z(), expected.z()) == 0 &&
                Float.compare(actual.yaw(), expected.yaw()) == 0 &&
                Float.compare(actual.pitch(), expected.pitch()) == 0;
        } catch (ReflectiveOperationException | RuntimeException failure) {
            return false;
        }
    }

    private static boolean nativePlayerPoseEquals(long handle,
                                                   EntityShadow expected) {
        return expected.world() != null &&
            NativeAccess.playerWorld(handle) == expected.world().nativeHandle() &&
            Double.compare(NativeAccess.coordinate(handle, 0), expected.x()) == 0 &&
            Double.compare(NativeAccess.coordinate(handle, 1), expected.y()) == 0 &&
            Double.compare(NativeAccess.coordinate(handle, 2), expected.z()) == 0 &&
            Float.compare(NativeAccess.entityRotation(handle, 0), expected.yaw()) == 0 &&
            Float.compare(NativeAccess.entityRotation(handle, 1), expected.pitch()) == 0;
    }

    private static boolean playerPoseMatchesWorld(ServerPlayerEntity player,
                                                   ServerWorld world) {
        try {
            EntityShadow shadow = captureEntityShadow(player);
            long handle = player.nativeHandle();
            return shadow.world() == world &&
                NativeAccess.playerWorld(handle) == world.nativeHandle() &&
                Double.compare(NativeAccess.coordinate(handle, 0), shadow.x()) == 0 &&
                Double.compare(NativeAccess.coordinate(handle, 1), shadow.y()) == 0 &&
                Double.compare(NativeAccess.coordinate(handle, 2), shadow.z()) == 0 &&
                Float.compare(NativeAccess.entityRotation(handle, 0), shadow.yaw()) == 0 &&
                Float.compare(NativeAccess.entityRotation(handle, 1), shadow.pitch()) == 0;
        } catch (ReflectiveOperationException | RuntimeException failure) {
            return false;
        }
    }

    private static boolean playerWorldChangeEventAdvanced(
            int previousCount, String originName, String destinationName) {
        String event = LAST_PLAYER_WORLD_CHANGE_EVENT.get();
        return PLAYER_WORLD_CHANGE_EVENT_COUNT.get() == previousCount + 1 &&
            event.contains("status=PASS origin=" + originName +
                          " destination=" + destinationName);
    }

    private static int runDimensionTransferProbe(
            com.mojang.brigadier.context.CommandContext<ServerCommandSource> context) {
        ServerCommandSource source = context.getSource();
        try {
            String phase = StringArgumentType.getString(context, "phase");
            long handle = Long.parseUnsignedLong(
                StringArgumentType.getString(context, "handle"));
            ServerPlayerEntity player = ServerPlayerEntity.of(handle);
            if (player == null) throw new IllegalStateException("player wrapper unavailable");

            if ("native_portal_arm".equals(phase)) {
                ServerWorld origin = player.getServerWorld();
                ServerWorld destination = ServerWorld.of(
                    NativeAccess.serverWorld(-1), source.getServer());
                boolean valid = origin != null && destination != null &&
                    NativeAccess.playerWorld(handle) == origin.nativeHandle() &&
                    origin.nativeHandle() == NativeAccess.serverWorld(0) &&
                    destination.nativeHandle() == NativeAccess.serverWorld(-1);
                if (valid) {
                    NATIVE_PORTAL_EXPECTED_PLAYER.set(player);
                    NATIVE_PORTAL_EXPECTED_ORIGIN.set(origin);
                    NATIVE_PORTAL_EXPECTED_DESTINATION.set(destination);
                    NATIVE_PORTAL_EXPECTED_EVENT_COUNT.set(
                        PLAYER_WORLD_CHANGE_EVENT_COUNT.get());
                }
                source.sendMessage(Text.literal(
                    "CPPFM_DIMENSION_TRANSFER_NATIVE_PORTAL_ARM:" +
                    (valid ? "PASS" : "FAIL")));
                return valid ? 1 : 0;
            }

            if ("native_portal_verify".equals(phase)) {
                ServerPlayerEntity expectedPlayer =
                    NATIVE_PORTAL_EXPECTED_PLAYER.getAndSet(null);
                ServerWorld expectedOrigin =
                    NATIVE_PORTAL_EXPECTED_ORIGIN.getAndSet(null);
                ServerWorld expectedDestination =
                    NATIVE_PORTAL_EXPECTED_DESTINATION.getAndSet(null);
                int expectedCount =
                    NATIVE_PORTAL_EXPECTED_EVENT_COUNT.getAndSet(-1);
                boolean passed = expectedPlayer == player &&
                    expectedOrigin != null && expectedDestination != null &&
                    LAST_PLAYER_WORLD_CHANGE_PLAYER.get() == player &&
                    LAST_PLAYER_WORLD_CHANGE_ORIGIN.get() == expectedOrigin &&
                    LAST_PLAYER_WORLD_CHANGE_DESTINATION.get() ==
                        expectedDestination &&
                    PLAYER_WORLD_CHANGE_EVENT_COUNT.get() == expectedCount + 1 &&
                    entityShadowEquals(player, new EntityShadow(
                        expectedDestination, player.getX(), player.getY(),
                        player.getZ(), player.getYaw(), player.getPitch())) &&
                    NativeAccess.playerWorld(handle) ==
                        expectedDestination.nativeHandle();
                source.sendMessage(Text.literal(
                    "CPPFM_DIMENSION_TRANSFER_NATIVE_PORTAL:" +
                    (passed ? "PASS" : "FAIL")));
                return passed ? 1 : 0;
            }

            if ("success".equals(phase)) {
                ServerWorld target = ServerWorld.of(
                    NativeAccess.serverWorld(-1), source.getServer());
                final double x = 31.25;
                final double y = 66.0;
                final double z = -8.75;
                final float yaw = 90.0f;
                final float pitch = 5.0f;
                int eventCountBefore = PLAYER_WORLD_CHANGE_EVENT_COUNT.get();
                ServerPlayerEntity returned = player.teleportTo(
                    new TeleportTarget(target, new Vec3d(x, y, z), yaw, pitch));
                EntityShadow expected = new EntityShadow(target, x, y, z,
                                                          yaw, pitch);
                boolean passed = returned == player &&
                    entityShadowEquals(player, expected) &&
                    nativePlayerPoseEquals(handle, expected) &&
                    player.getWorld() == target &&
                    Double.compare(player.getX(), x) == 0 &&
                    Double.compare(player.getY(), y) == 0 &&
                    Double.compare(player.getZ(), z) == 0 &&
                    Float.compare(player.getYaw(), yaw) == 0 &&
                    Float.compare(player.getPitch(), pitch) == 0 &&
                    playerWorldChangeEventAdvanced(eventCountBefore,
                        "minecraft:overworld", "minecraft:the_nether");
                source.sendMessage(Text.literal(
                    "CPPFM_DIMENSION_TRANSFER_SUCCESS:" +
                    (passed ? "PASS" : "FAIL")));
                return passed ? 1 : 0;
            }

            if ("return".equals(phase)) {
                ServerWorld target = ServerWorld.of(
                    NativeAccess.serverWorld(0), source.getServer());
                final double x = -12.5;
                final double y = 72.0;
                final double z = 4.5;
                final float yaw = 15.0f;
                final float pitch = 2.0f;
                int eventCountBefore = PLAYER_WORLD_CHANGE_EVENT_COUNT.get();
                ServerPlayerEntity returned = player.teleportTo(
                    new TeleportTarget(target, new Vec3d(x, y, z), yaw, pitch));
                EntityShadow expected = new EntityShadow(target, x, y, z,
                                                          yaw, pitch);
                boolean passed = returned == player &&
                    entityShadowEquals(player, expected) &&
                    nativePlayerPoseEquals(handle, expected) &&
                    playerWorldChangeEventAdvanced(eventCountBefore,
                        "minecraft:the_nether", "minecraft:overworld");
                source.sendMessage(Text.literal(
                    "CPPFM_DIMENSION_TRANSFER_RETURN:" +
                    (passed ? "PASS" : "FAIL")));
                return passed ? 1 : 0;
            }

            if ("same_world".equals(phase)) {
                EntityShadow before = captureEntityShadow(player);
                int eventCountBefore = PLAYER_WORLD_CHANGE_EVENT_COUNT.get();
                ServerPlayerEntity returned = player.teleportTo(
                    new TeleportTarget(player.getServerWorld(),
                        new Vec3d(before.x(), before.y(), before.z()),
                        before.yaw(), before.pitch()));
                boolean passed = returned == player &&
                    entityShadowEquals(player, before) &&
                    nativePlayerPoseEquals(handle, before) &&
                    PLAYER_WORLD_CHANGE_EVENT_COUNT.get() == eventCountBefore;
                source.sendMessage(Text.literal(
                    "CPPFM_DIMENSION_TRANSFER_SAME_WORLD:" +
                    (passed ? "PASS" : "FAIL")));
                return passed ? 1 : 0;
            }

            if ("reject".equals(phase)) {
                EntityShadow before = captureEntityShadow(player);
                int eventCountBefore = PLAYER_WORLD_CHANGE_EVENT_COUNT.get();
                boolean rejected = false;
                try {
                    player.teleportTo(new TeleportTarget(
                        ServerWorld.of(NativeAccess.serverWorld(1),
                                       source.getServer()),
                        new Vec3d(-100.5, 90.0, 40.25), -30.0f, 22.0f));
                } catch (IllegalStateException expected) {
                    rejected = true;
                }
                boolean passed = rejected && entityShadowEquals(player, before) &&
                    nativePlayerPoseEquals(handle, before) &&
                    PLAYER_WORLD_CHANGE_EVENT_COUNT.get() == eventCountBefore;
                source.sendMessage(Text.literal(
                    "CPPFM_DIMENSION_TRANSFER_REJECT:" +
                    (passed ? "PASS" : "FAIL")));
                return passed ? 1 : 0;
            }
            source.sendMessage(Text.literal("CPPFM_DIMENSION_TRANSFER:BAD_PHASE"));
        } catch (Throwable failure) {
            source.sendMessage(Text.literal(
                "CPPFM_DIMENSION_TRANSFER:ERROR:" +
                failure.getClass().getSimpleName()));
        }
        return 0;
    }

    @Override
    public void onInitializeServer() {
        NativeBridge.nativeLog("INFO", "fixture entrypoint initialized");
        ServerEntityWorldChangeEvents.AFTER_PLAYER_CHANGE_WORLD.register(
            (player, origin, destination) -> {
                LAST_PLAYER_WORLD_CHANGE_PLAYER.set(player);
                LAST_PLAYER_WORLD_CHANGE_ORIGIN.set(origin);
                LAST_PLAYER_WORLD_CHANGE_DESTINATION.set(destination);
                boolean consistent = playerPoseMatchesWorld(player, destination);
                String originName = NativeAccess.worldName(origin.nativeHandle());
                String destinationName =
                    NativeAccess.worldName(destination.nativeHandle());
                String marker = "fixture PLAYER_WORLD_CHANGE status=" +
                    (consistent ? "PASS" : "FAIL") + " origin=" + originName +
                    " destination=" + destinationName + " native=" +
                    (NativeAccess.playerWorld(player.nativeHandle()) ==
                        destination.nativeHandle()) + " x=" + player.getX() +
                    " y=" + player.getY() + " z=" + player.getZ();
                LAST_PLAYER_WORLD_CHANGE_EVENT.set(marker);
                PLAYER_WORLD_CHANGE_EVENT_COUNT.incrementAndGet();
                NativeBridge.nativeLog("INFO", marker);
            });
        ResourceManagerHelper.SERVER_DATA.registerReloadListener(new IdentifiableResourceReloadListener() {
            @Override public Identifier getFabricId() {
                return Identifier.of("cppfm_fixture", "reload_success");
            }

            @Override
            public CompletableFuture<Void> reload(ResourceReloader.Synchronizer synchronizer,
                                                   ResourceManager manager,
                                                   Executor prepareExecutor,
                                                   Executor applyExecutor) {
                return CompletableFuture.supplyAsync(() -> {
                    boolean resourcePresent = manager.getResource(
                        Identifier.of("minecraft", "recipes/acacia_boat.json")).isPresent();
                    NativeBridge.nativeLog("INFO",
                        "fixture RELOAD_SUCCESS resource=" + resourcePresent);
                    if (!resourcePresent)
                        throw new IllegalStateException("fixture resource lookup failed");
                    return (Void) null;
                }, prepareExecutor).thenCompose(
                    prepared -> synchronizer.whenPrepared(prepared))
                    .thenRunAsync(() -> { }, applyExecutor);
            }
        });
        ResourceManagerHelper.SERVER_DATA.registerReloadListener(new IdentifiableResourceReloadListener() {
            @Override public Identifier getFabricId() {
                return Identifier.of("cppfm_fixture", "reload_failure");
            }

            @Override
            public CompletableFuture<Void> reload(ResourceReloader.Synchronizer synchronizer,
                                                   ResourceManager manager,
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
                                                   Executor prepareExecutor,
                                                   Executor applyExecutor) {
                NativeBridge.nativeLog("INFO", "fixture RELOAD_AFTER_FAILURE");
                return CompletableFuture.supplyAsync(() -> (Void) null,
                    prepareExecutor).thenCompose(
                        prepared -> synchronizer.whenPrepared(prepared))
                    .thenRunAsync(() -> { }, applyExecutor);
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
            if (server.getTicks() == 1 &&
                    WORKER_ENTITY_QUERY_PROBE_REPORTED.compareAndSet(false, true)) {
                verifyWorkerEntityQuery(server.getOverworld());
            }
            if (!PLAYER_ROTATION_PROBE_REPORTED.get()) {
                for (ServerPlayerEntity player :
                        server.getPlayerManager().getPlayerList()) {
                    if (!"JvmReenterA".equals(
                            player.getName().getString())) continue;
                    double beforeX = player.getX();
                    double beforeY = player.getY();
                    double beforeZ = player.getZ();
                    float yaw = player.getYaw();
                    float pitch = player.getPitch();
                    if (yaw < 70.0f || yaw > 110.0f ||
                        pitch < 10.0f || pitch > 35.0f) continue;

                    double targetX = beforeX + 2.25;
                    double targetY = beforeY;
                    double targetZ = beforeZ + 1.25;
                    float requestedYaw = yaw + 17.25f;
                    float requestedPitch = pitch + 6.5f;
                    player.setPosition(targetX, targetY, targetZ);
                    player.setYaw(requestedYaw);
                    player.setPitch(requestedPitch);
                    boolean settersReachNativeState =
                        Math.abs(NativeAccess.coordinate(
                            player.nativeHandle(), 0) - targetX) < 0.01 &&
                        Math.abs(NativeAccess.coordinate(
                            player.nativeHandle(), 1) - targetY) < 0.01 &&
                        Math.abs(NativeAccess.coordinate(
                            player.nativeHandle(), 2) - targetZ) < 0.01 &&
                        Math.abs(NativeAccess.entityRotation(
                            player.nativeHandle(), 0) - requestedYaw) < 0.01f &&
                        Math.abs(NativeAccess.entityRotation(
                            player.nativeHandle(), 1) - requestedPitch) < 0.01f;
                    NativeBridge.nativeLog("INFO",
                        "fixture PLAYER_POSE_NATIVE beforeX=" + beforeX +
                        " beforeY=" + beforeY + " beforeZ=" + beforeZ +
                        " beforeYaw=" + yaw + " beforePitch=" + pitch +
                        " targetX=" + targetX + " targetY=" + targetY +
                        " targetZ=" + targetZ + " targetYaw=" + requestedYaw +
                        " targetPitch=" + requestedPitch + " native=" +
                        settersReachNativeState);
                    PLAYER_ROTATION_PROBE_REPORTED.set(true);
                    break;
                }
            }
            if (NATIVE_QUIT_RECHECK_TICKS.updateAndGet(value ->
                    value > 0 ? value - 1 : value) == 0) {
                long staleHandle = NATIVE_QUIT_PLAYER_HANDLE.getAndSet(0L);
                if (staleHandle != 0L) {
                    boolean rejected = ServerPlayerEntity.of(staleHandle) == null &&
                        NATIVE_QUIT_INIT_COUNT.get() == 1;
                    NativeBridge.nativeLog("INFO",
                        "fixture NATIVE_QUIT_STALE_WRAPPER_REJECTED " + rejected);
                }
            }
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
                    boolean unhealedVetoCall = victim.damage(
                        new net.minecraft.entity.damage.DamageSource(
                            "fixture_death_veto_unhealed"), 100.0f);
                    boolean unhealedVeto = unhealedVetoCall &&
                        victim.isDead() && !victim.isAlive() &&
                        NativeAccess.entityHealth(handle) == 0.0f &&
                        DAMAGE_UNHEALED_VETO_CALLBACKS.get() == 1 &&
                        DAMAGE_AFTER_UNHEALED_VETO_CALLBACKS.get() == 0 &&
                        DAMAGE_AFTER_UNHEALED_DEATH_CALLBACKS.get() == 0;
                    boolean restoredForFollowingProbe =
                        NativeAccess.setEntityHealth(handle, 4.0f) &&
                        victim.isAlive() && !victim.isDead();
                    NativeBridge.nativeLog("INFO",
                        "fixture DAMAGE_BRIDGE_UNHEALED_VETO " +
                        (unhealedVeto && restoredForFollowingProbe));

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
                if (setup) {
                    net.minecraft.entity.Entity.RemovalReason[] reasons = {
                        net.minecraft.entity.Entity.RemovalReason.UNLOADED_TO_CHUNK,
                        net.minecraft.entity.Entity.RemovalReason.UNLOADED_WITH_PLAYER,
                        net.minecraft.entity.Entity.RemovalReason.CHANGED_DIMENSION
                    };
                    boolean unsupportedRemovalReasonsFailClosed = true;
                    for (int i = 0; i < reasons.length; ++i) {
                        long previousHandle = LAST_LOADED_PIG_HANDLE.get();
                        boolean spawned = NativeBridge.nativeExecuteCommand(
                            "summon minecraft:pig " + (100 + i) + " -60 0");
                        long removalHandle = LAST_LOADED_PIG_HANDLE.get();
                        net.minecraft.entity.Entity entity =
                            net.minecraft.entity.Entity.of(removalHandle);
                        boolean freshEntity = spawned && removalHandle != 0L &&
                            removalHandle != previousHandle && entity != null;
                        if (freshEntity) entity.remove(reasons[i]);
                        unsupportedRemovalReasonsFailClosed &= freshEntity &&
                            !entity.isRemoved() && entity.isAlive();
                    }
                    NativeBridge.nativeLog("INFO",
                        "fixture NATIVE_UNSUPPORTED_REMOVAL_REASONS_FAIL_CLOSED " +
                        unsupportedRemovalReasonsFailClosed);
                }
            }
            if (server.getTicks() == 4)
                startWorkerEntityRetirementProbe(server);
            if (server.getTicks() >= 5)
                finishWorkerEntityRetirementProbe(server.getOverworld(),
                                                   server.getTicks());
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
            dispatcher.register(CommandManager.<ServerCommandSource>literal(
                    "cppfm_world_setblock_probe")
                .executes(context -> {
                    ServerPlayerEntity player = context.getSource().getPlayer();
                    if (player == null) {
                        NativeBridge.nativeLog("ERROR",
                            "fixture WORLD_SET_BLOCK_API status=FAIL reason=not-player");
                        return 0;
                    }
                    World world = player.getWorld();
                    BlockPos pos = player.getBlockPos().add(4, 3, 0);
                    int goldState = NativeAccess.registryBlockState(
                        "minecraft:gold_block");
                    BlockState gold = new BlockState(goldState);
                    world.setBlockState(pos,
                        net.minecraft.block.Blocks.STONE.getDefaultState(), 0, 512);
                    boolean preparationStored = world.getBlockState(pos)
                        .isOf(net.minecraft.block.Blocks.STONE);
                    boolean invalidYAccepted = world.setBlockState(
                        new BlockPos(pos.getX(), 1000, pos.getZ()), gold, 3, 512);
                    boolean changed = goldState >= 0 && world.setBlockState(
                        pos, gold, 3, 512);
                    boolean unchangedAccepted = goldState >= 0 &&
                        world.setBlockState(pos, gold, 3, 512);
                    boolean invalidStateAccepted = NativeAccess.setWorldBlock(
                        world.nativeHandle(), pos.getX(), pos.getY(), pos.getZ(),
                        -1, 3, 512);
                    boolean stored = changed && world.getBlockState(pos)
                        .getRawState() == goldState;
                    boolean passed = goldState >= 0 && preparationStored && changed &&
                        !unchangedAccepted && !invalidStateAccepted && stored &&
                        !invalidYAccepted;
                    NativeBridge.nativeLog(passed ? "INFO" : "ERROR",
                        "fixture WORLD_SET_BLOCK_API status=" +
                        (passed ? "PASS" : "FAIL") + " changed=" + changed +
                        " preparationStored=" + preparationStored +
                        " unchangedAccepted=" +
                        unchangedAccepted + " invalidStateAccepted=" +
                        invalidStateAccepted +
                        " invalidYAccepted=" + invalidYAccepted + " stored=" +
                        stored + " pos=" + pos.getX() + "," + pos.getY() +
                        "," + pos.getZ() + " state=" + goldState);
                    return passed ? 1 : 0;
                }));
            dispatcher.register(CommandManager.<ServerCommandSource>literal(
                    "cppfm_dimension_transfer")
                .then(CommandManager.<ServerCommandSource, String>argument(
                        "phase", StringArgumentType.word())
                    .then(CommandManager.<ServerCommandSource, String>argument(
                            "handle", StringArgumentType.word())
                        .executes(FixtureMod::runDimensionTransferProbe))));
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
            if ("minecraft:cod".equals(type) ||
                "minecraft:squid".equals(type) ||
                "minecraft:glow_squid".equals(type)) {
                NativeBridge.nativeLog("INFO",
                    "fixture AQUATIC_WRAPPER type=" + type +
                    " fish=" + (entity instanceof FishEntity) +
                    " waterAnimal=" + (entity instanceof
                        net.minecraft.entity.passive.WaterAnimalEntity) +
                    " squid=" + (entity instanceof
                        net.minecraft.entity.passive.SquidEntity) +
                    " glowSquid=" + (entity instanceof
                        net.minecraft.entity.passive.GlowSquidEntity));
            }
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
            if ("fixture_death_veto_unhealed".equals(source.getName())) {
                DAMAGE_UNHEALED_VETO_CALLBACKS.incrementAndGet();
                return false;
            }
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
                if ("fixture_death_veto_unhealed".equals(source.getName()))
                    DAMAGE_AFTER_UNHEALED_VETO_CALLBACKS.incrementAndGet();
                if ("fixture_death_allow".equals(source.getName()))
                    DAMAGE_AFTER_ALLOW_CALLBACKS.incrementAndGet();
            });
        ServerLivingEntityEvents.AFTER_DEATH.register((entity, source) -> {
            if (entity.nativeHandle() != DAMAGE_PROBE_PIG_HANDLE.get()) return;
            if ("fixture_death_veto_unhealed".equals(source.getName()))
                DAMAGE_AFTER_UNHEALED_DEATH_CALLBACKS.incrementAndGet();
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
        ServerPlayConnectionEvents.INIT.register((handler, server) -> {
            if (handler != null && handler.getPlayer() != null &&
                "JvmQuitProbe".equals(
                    handler.getPlayer().getName().getString()))
                NATIVE_QUIT_INIT_COUNT.incrementAndGet();
        });
        ServerPlayConnectionEvents.DISCONNECT.register((handler, server) -> {
            recordCallbackThread("player_disconnect");
            if ("JvmJoinKick".equals(handler.getPlayer().getName().getString()))
                NativeBridge.nativeLog("INFO", "fixture JOIN_KICK_DISCONNECTED");
            if ("JvmQuitProbe".equals(
                    handler.getPlayer().getName().getString()))
                NATIVE_QUIT_PLAYER_HANDLE.set(
                    handler.getPlayer().nativeHandle());
            if ("JvmQuitProbe".equals(
                    handler.getPlayer().getName().getString())) {
                NATIVE_QUIT_RECHECK_TICKS.set(3);
                NativeBridge.nativeLog("INFO",
                    "fixture NATIVE_QUIT_HANDLER_DISCONNECTED " +
                    handler.isDisconnected());
            }
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
