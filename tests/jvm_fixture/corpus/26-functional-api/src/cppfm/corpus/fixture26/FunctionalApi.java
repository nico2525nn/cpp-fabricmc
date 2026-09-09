package cppfm.corpus.fixture26;

import cppfm.bridge.NativeBridge;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.command.v2.CommandRegistrationCallback;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerWorldEvents;
import net.fabricmc.fabric.api.networking.v1.PayloadTypeRegistry;
import net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking;
import net.minecraft.block.AbstractBlock;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.item.Item;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.network.RegistryByteBuf;
import net.minecraft.network.codec.PacketCodec;
import net.minecraft.network.codec.PacketCodecs;
import net.minecraft.network.packet.CustomPayload;
import net.minecraft.registry.Registry;
import net.minecraft.registry.Registries;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.command.CommandManager;
import net.minecraft.server.command.ServerCommandSource;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.BlockPos;

/**
 * P2 functional server-side fixture for the current bounded shadow API.
 *
 * <p>This is deliberately an auxiliary mod. The strict compatibility report
 * owns cases 01..25; the smoke runner stages this mod beside them and checks
 * the FUNCTIONAL_FIXTURE evidence independently.</p>
 */
public final class FunctionalApi implements DedicatedServerModInitializer {
    private static final Identifier BLOCK_ID = Identifier.of("corpus26", "functional_block");
    private static final Identifier ITEM_ID = Identifier.of("corpus26", "functional_item");
    private static final Identifier PAYLOAD_ID = Identifier.of("corpus26", "probe");
    private static final Identifier OVERWORLD_ID = Identifier.of("minecraft", "overworld");
    private static final BlockPos PROBE_POS = new BlockPos(19, -60, 19);
    private static final String COMMAND = "corpus26_probe";
    private static final int PAYLOAD_VALUE = 769;

    private static final AtomicBoolean REGISTRY_OK = new AtomicBoolean();
    private static final AtomicBoolean WORLD_LOAD_OK = new AtomicBoolean();
    private static final AtomicBoolean WORLD_STARTED_OK = new AtomicBoolean();
    private static final AtomicBoolean WORLD_STATE_OK = new AtomicBoolean();
    private static final AtomicBoolean PAYLOAD_OK = new AtomicBoolean();
    private static final AtomicBoolean PAYLOAD_RECEIVED = new AtomicBoolean();
    private static final AtomicBoolean COMMAND_REGISTERED = new AtomicBoolean();
    private static final AtomicBoolean COMMAND_EXECUTED = new AtomicBoolean();
    private static final AtomicBoolean TICK_OK = new AtomicBoolean();
    private static final AtomicBoolean WORLD_UNLOAD_OK = new AtomicBoolean();
    private static final AtomicBoolean SAVE_CALLBACK_SEEN = new AtomicBoolean();
    private static final AtomicBoolean FIRST_TICK_HANDLED = new AtomicBoolean();
    private static final AtomicInteger TICK_COUNT = new AtomicInteger();
    private static final NbtCompound SAVED_STATE = new NbtCompound();

    private static final PacketCodec<RegistryByteBuf, ProbePayload> PAYLOAD_CODEC = PacketCodec.of(
        (ProbePayload value, RegistryByteBuf buffer) -> buffer.writeVarInt(value.value()),
        (RegistryByteBuf buffer) -> new ProbePayload(buffer.readVarInt()));

    private static Block registeredBlock;

    private static void result(String phase, boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "FUNCTIONAL_FIXTURE status=" + (ok ? "PASS" : "FAIL") + " phase=" + phase);
    }

    private static void result(String phase, boolean ok, String detail) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "FUNCTIONAL_FIXTURE status=" + (ok ? "PASS" : "FAIL")
                + " phase=" + phase + " detail=" + detail);
    }

    private static void diagnostic(String phase, String detail) {
        NativeBridge.nativeLog("WARN",
            "FUNCTIONAL_FIXTURE status=DIAGNOSTIC phase=" + phase + " detail=" + detail);
    }

    // CppModRuntime tears down the JNI native bridge before invoking the final
    // lifecycle callbacks. Keep shutdown evidence on the Java-owned stream so
    // the process harness can still collect it.
    private static void shutdownResult(String phase, boolean ok) {
        System.out.println("FUNCTIONAL_FIXTURE status=" + (ok ? "PASS" : "FAIL")
            + " phase=" + phase);
        System.out.flush();
    }

    private static void shutdownDiagnostic(String phase, String detail) {
        System.out.println("FUNCTIONAL_FIXTURE status=DIAGNOSTIC phase=" + phase
            + " detail=" + detail);
        System.out.flush();
    }

    private static String failureType(Throwable failure) {
        return failure == null ? "unknown" : failure.getClass().getSimpleName();
    }

    @Override
    public void onInitializeServer() {
        registerLifecycleCallbacks();
        registerCommand();
        registerPayload();
        probeRegistry();
    }

    private static void registerLifecycleCallbacks() {
        ServerWorldEvents.LOAD.register((server, world) -> {
            boolean ok = server != null && world != null && world.getServer() == server
                && OVERWORLD_ID.equals(world.getRegistryKey().getValue());
            WORLD_LOAD_OK.set(ok);
            result("world-load", ok);
        });

        ServerLifecycleEvents.SERVER_STARTED.register(FunctionalApi::onServerStarted);

        ServerTickEvents.END.register(server -> {
            int observed = TICK_COUNT.incrementAndGet();
            if (!FIRST_TICK_HANDLED.compareAndSet(false, true)) return;

            boolean tickOk = server != null && observed == 1 && server.getTicks() == 1;
            TICK_OK.set(tickOk);
            result("tick-counter", tickOk, "count=" + observed + ";serverTick="
                + (server == null ? -1 : server.getTicks()));

            boolean nativeReturn = false;
            try {
                nativeReturn = NativeBridge.nativeExecuteCommand(COMMAND);
            } catch (Throwable failure) {
                diagnostic("command-execution-error", failureType(failure));
            }
            boolean commandOk = nativeReturn && COMMAND_REGISTERED.get()
                && COMMAND_EXECUTED.get();
            result("command-execution", commandOk,
                "nativeReturn=" + nativeReturn + ";handler=" + COMMAND_EXECUTED.get());

            boolean complete = REGISTRY_OK.get()
                && WORLD_LOAD_OK.get()
                && WORLD_STARTED_OK.get()
                && WORLD_STATE_OK.get()
                && PAYLOAD_OK.get()
                && TICK_OK.get()
                && commandOk;
            result("functional-complete", complete);
        });

        ServerWorldEvents.UNLOAD.register((server, world) -> {
            boolean ok = server != null && world != null && WORLD_LOAD_OK.get()
                && OVERWORLD_ID.equals(world.getRegistryKey().getValue());
            WORLD_UNLOAD_OK.set(ok);
            shutdownResult("world-unload", ok);
        });

        ServerLifecycleEvents.BEFORE_SAVE.register((server, flush, skipErrors) -> {
            SAVE_CALLBACK_SEEN.set(true);
            boolean ok = SAVED_STATE.getInt("schema", 0) == 1
                && SAVED_STATE.getInt("block_state", -1) >= 0;
            result("before-save-state", ok, "flush=" + flush + ";skipErrors=" + skipErrors);
        });

        ServerLifecycleEvents.SERVER_STOPPED.register(server -> shutdownDiagnostic(
            "persistent-state-boundary",
            SAVE_CALLBACK_SEEN.get()
                ? "before-save-observed;PersistentStateManager-not-exposed-by-bounded-shadow-api"
                : "PersistentStateManager-and-native-save-round-trip-not-exposed-by-bounded-shadow-api"));
    }

    private static void onServerStarted(MinecraftServer server) {
        ServerWorld world = server == null ? null : server.getOverworld();
        boolean lifecycleOk = server != null && world != null && world.getServer() == server
            && WORLD_LOAD_OK.get();
        WORLD_STARTED_OK.set(lifecycleOk);
        result("server-world-started", lifecycleOk);
        if (world != null) probeWorldState(world);
        else result("world-state-read-write", false, "overworld-unavailable");
        probeNbtRoundTrip();
    }

    private static void registerCommand() {
        CommandRegistrationCallback.EVENT.register((dispatcher, registryAccess, environment) -> {
            try {
                var node = dispatcher.register(CommandManager.<ServerCommandSource>
                    literal(COMMAND).executes(context -> {
                        COMMAND_EXECUTED.set(true);
                        return 26;
                    }));
                boolean ok = node != null
                    && environment == CommandManager.RegistrationEnvironment.DEDICATED;
                COMMAND_REGISTERED.set(ok);
                result("command-registration", ok);
            } catch (Throwable failure) {
                result("command-registration", false, failureType(failure));
            }
        });
    }

    private static void registerPayload() {
        try {
            PayloadTypeRegistry<RegistryByteBuf> registry = PayloadTypeRegistry.playC2S();
            CustomPayload.Type<ProbePayload> type = registry.register(
                ProbePayload.ID, PAYLOAD_CODEC);
            ServerPlayNetworking.PlayPayloadHandler<ProbePayload> receiver = (payload, context) -> {
                if (payload != null && payload.value() == PAYLOAD_VALUE)
                    PAYLOAD_RECEIVED.set(true);
            };
            boolean receiverRegistered = ServerPlayNetworking.registerGlobalReceiver(
                ProbePayload.ID, receiver);

            ProbePayload source = new ProbePayload(PAYLOAD_VALUE);
            RegistryByteBuf encoded = new RegistryByteBuf();
            PAYLOAD_CODEC.encode(encoded, source);
            ProbePayload decoded = PAYLOAD_CODEC.decode(
                new RegistryByteBuf(encoded.toByteArray()));
            // The decoded-payload API indexes per-connection receivers by the
            // handler object. A null handler is not a valid execution path for
            // ConcurrentHashMap-backed receiver dispatch, so use the public
            // constructor with a null player rather than modifying production
            // networking semantics or inventing a player.
            ServerPlayNetworkHandler testHandler = new ServerPlayNetworkHandler(
                (net.minecraft.server.network.ServerPlayerEntity) null);
            boolean dispatched = ServerPlayNetworking.receive(null, null, testHandler, decoded);
            boolean ok = type != null
                && ProbePayload.ID.equals(type.id())
                && registry.contains(ProbePayload.ID)
                && registry.getCodec(ProbePayload.ID) == PAYLOAD_CODEC
                && decoded.value() == PAYLOAD_VALUE
                && receiverRegistered
                && dispatched
                && PAYLOAD_RECEIVED.get();
            PAYLOAD_OK.set(ok);
            result("payload-codec-dispatch", ok,
                "bytes=" + encoded.readableBytes() + ";received=" + PAYLOAD_RECEIVED.get());
        } catch (Throwable failure) {
            result("payload-codec-dispatch", false, failureType(failure));
        }
    }

    private static void probeRegistry() {
        try {
            Block block = new Block(AbstractBlock.Settings.create());
            Item item = new Item(new Item.Settings());
            registeredBlock = Registry.register(Registries.BLOCK, BLOCK_ID, block);
            Item registeredItem = Registry.register(Registries.ITEM, ITEM_ID, item);
            boolean blockEntry = Registries.BLOCK.getEntry(BLOCK_ID)
                .map(entry -> entry.value() == block && entry.matchesId(BLOCK_ID))
                .orElse(false);
            boolean itemEntry = Registries.ITEM.getEntry(ITEM_ID)
                .map(entry -> entry.value() == item && entry.matchesId(ITEM_ID))
                .orElse(false);
            boolean ok = registeredBlock == block
                && registeredItem == item
                && Registries.BLOCK.get(BLOCK_ID) == block
                && Registries.ITEM.get(ITEM_ID) == item
                && BLOCK_ID.equals(Registries.BLOCK.getId(block))
                && ITEM_ID.equals(Registries.ITEM.getId(item))
                && Registries.BLOCK.getRawId(block) >= 0
                && vanillaBlockLookupIsConsistent()
                && blockEntry
                && itemEntry;
            REGISTRY_OK.set(ok);
            result("registry-lookup", ok);
        } catch (Throwable failure) {
            result("registry-lookup", false, failureType(failure));
        }
    }

    private static boolean vanillaBlockLookupIsConsistent() {
        Block vanillaAir = Registries.BLOCK.get(Identifier.ofVanilla("air"));
        return vanillaAir != null
            && Identifier.ofVanilla("air").equals(Registries.BLOCK.getId(vanillaAir));
    }

    private static void probeWorldState(ServerWorld world) {
        try {
            int before = world.getBlockState(PROBE_POS).getRawId();
            BlockState desired = new BlockState(1234);
            boolean wrote = world.setBlockState(PROBE_POS, desired, 3);
            int after = world.getBlockState(PROBE_POS).getRawId();
            boolean ok = wrote && after == desired.getRawId()
                && world.getBlockState(PROBE_POS).getRawId() == after
                && OVERWORLD_ID.equals(world.getRegistryKey().getValue());
            WORLD_STATE_OK.set(ok);
            result("world-state-read-write", ok, "before=" + before + ";after=" + after);
        } catch (Throwable failure) {
            result("world-state-read-write", false, failureType(failure));
        }
    }

    private static void probeNbtRoundTrip() {
        try {
            SAVED_STATE.putInt("schema", 1)
                .putString("owner", "corpus26")
                .putInt("block_state", 1234)
                .putInt("tick_count", TICK_COUNT.get());
            PacketByteBuf encoded = new PacketByteBuf();
            PacketCodecs.NBT_COMPOUND.encode(encoded, SAVED_STATE);
            NbtCompound decoded = PacketCodecs.NBT_COMPOUND.decode(
                new PacketByteBuf(encoded.toByteArray()));
            boolean ok = decoded != null
                && decoded.getInt("schema", 0) == 1
                && "corpus26".equals(decoded.getString("owner"))
                && decoded.getInt("block_state", -1) == 1234
                && decoded.getInt("tick_count", -1) == 0;
            result("nbt-state-roundtrip", ok,
                "bytes=" + encoded.readableBytes() + ";keys=" + decoded.getKeys().size());
        } catch (Throwable failure) {
            result("nbt-state-roundtrip", false, failureType(failure));
        }
    }

    private record ProbePayload(int value) implements CustomPayload {
        private static final CustomPayload.Id<ProbePayload> ID =
            new CustomPayload.Id<>(PAYLOAD_ID);

        @Override
        public CustomPayload.Id<ProbePayload> getId() {
            return ID;
        }
    }
}
