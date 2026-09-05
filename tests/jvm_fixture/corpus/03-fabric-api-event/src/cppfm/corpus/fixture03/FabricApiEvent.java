package cppfm.corpus.fixture03;

import cppfm.bridge.NativeBridge;
import cppfm.api.ModEvents;
import java.util.concurrent.atomic.AtomicBoolean;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerWorldEvents;
import net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking;
import net.minecraft.network.PacketByteBuf;
import net.minecraft.util.Identifier;

/** Corpus 03: lifecycle, tick, world, and bounded networking API callbacks. */
public final class FabricApiEvent implements DedicatedServerModInitializer {
    private static final Identifier CHANNEL = Identifier.of("corpus03", "probe");
    private static final AtomicBoolean START_TICK_SEEN = new AtomicBoolean();
    private static final AtomicBoolean END_TICK_SEEN = new AtomicBoolean();

    private static void result(String phase, boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=03 status=" + (ok ? "PASS" : "FAIL") + " phase=" + phase);
    }

    // C++ clears the JNI runtime pointer immediately before shutdown callbacks.
    // Keep shutdown evidence on the Java stdout stream so it remains visible
    // to the owned-process harness without weakening that lifecycle boundary.
    private static void shutdownResult(String phase) {
        System.out.println("CORPUS case=03 status=PASS phase=" + phase);
        System.out.flush();
    }

    @Override
    public void onInitializeServer() {
        ServerLifecycleEvents.SERVER_STARTING.register(server ->
            result("server-starting", true));
        ServerWorldEvents.LOAD.register((server, world) ->
            result("world-load-" + world.getRegistryKey().getValue(), true));
        ServerLifecycleEvents.SERVER_STARTED.register(server ->
            result("server-started", true));
        ServerTickEvents.START.register(server -> {
            if (server.getTicks() == 1 && START_TICK_SEEN.compareAndSet(false, true))
                result("tick-start-1", true);
        });
        ServerTickEvents.END.register(server -> {
            if (server.getTicks() == 1 && END_TICK_SEEN.compareAndSet(false, true)) {
                result("tick-end-1", true);
                boolean handled = NativeBridge.nativeExecuteCommand("corpus_cancel");
                result("command-cancel-return-" + handled, handled);
            }
        });
        ServerWorldEvents.UNLOAD.register((server, world) ->
            shutdownResult("world-unload"));
        ServerLifecycleEvents.SERVER_STOPPING.register(server ->
            shutdownResult("server-stopping"));
        ServerLifecycleEvents.SERVER_STOPPED.register(server ->
            shutdownResult("server-stopped"));

        ServerPlayNetworking.registerGlobalReceiver(CHANNEL,
            (server, player, handler, buffer, responseSender) -> result("packet-receive", true));
        ModEvents.COMMAND.register((player, command) -> {
            if ("corpus_cancel".equals(command)) {
                result("command-cancel-callback", true);
                return null;
            }
            return command;
        });
        PacketByteBuf payload = new PacketByteBuf()
            .writeVarInt(300)
            .writeString("ok");
        byte[] encoded = payload.toByteArray();
        result("packet-buffer", encoded.length == 5 && (encoded[0] & 0xff) == 0xac
            && !ServerPlayNetworking.canSend(null, CHANNEL));
    }
}
