package cppfm.corpus.fixture22;

import cppfm.bridge.NativeBridge;
import java.util.concurrent.atomic.AtomicBoolean;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.command.v2.CommandRegistrationCallback;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.minecraft.server.command.CommandManager;

/** Corpus 22: a Java callback re-enters C++ and the Java command dispatcher. */
public final class ReentrantCallback implements DedicatedServerModInitializer {
    private static final AtomicBoolean SEEN = new AtomicBoolean();

    private static void result(String phase, boolean ok) {
        NativeBridge.nativeLog(ok ? "INFO" : "ERROR",
            "CORPUS case=22 status=" + (ok ? "PASS" : "FAIL") + " phase=" + phase);
    }

    @Override
    public void onInitializeServer() {
        CommandRegistrationCallback.EVENT.register((dispatcher, registry, environment) ->
            dispatcher.register(CommandManager.<net.minecraft.server.command.ServerCommandSource>
                literal("corpus_reentrant").executes(context -> {
                    result("nested-java-command", true);
                    return 1;
                })));
        ServerTickEvents.END.register(server -> {
            if (server.getTicks() != 1 || !SEEN.compareAndSet(false, true)) return;
            result("outer-callback", true);
            boolean returned = NativeBridge.nativeExecuteCommand("corpus_reentrant");
            result("native-reentry-return-" + returned, returned);
        });
    }
}
