package cppfm.fixture;

import cppfm.bridge.NativeBridge;
import cppfm.api.ModEvents;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.command.v2.CommandRegistrationCallback;
import net.minecraft.block.AbstractBlock;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.registry.Registry;
import net.minecraft.registry.Registries;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.server.command.CommandManager;
import net.minecraft.server.command.ServerCommandSource;
import com.mojang.brigadier.arguments.IntegerArgumentType;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.BlockPos;

/** Deterministic integration fixture for the embedded loader and callbacks. */
public final class FixtureMod implements DedicatedServerModInitializer {
    @Override
    public void onInitializeServer() {
        NativeBridge.nativeLog("INFO", "fixture entrypoint initialized");
        Block registered = Registry.register(Registries.BLOCK,
            Identifier.of("cppfm_fixture", "probe"),
            new Block(AbstractBlock.Settings.create()));
        ServerLifecycleEvents.SERVER_STARTED.register(server -> {
            ServerWorld world = server.getOverworld();
            BlockPos pos = new BlockPos(0, -60, 0);
            BlockState before = world.getBlockState(pos);
            world.setBlockState(pos, before);
            NativeBridge.nativeLog("INFO", "fixture WORLD_API " + registered.getId());
        });
        ServerLifecycleEvents.SERVER_STARTED.register(server ->
            NativeBridge.nativeLog("INFO", "fixture SERVER_STARTED"));
        ServerTickEvents.END.register(server -> {
            if (server.getTicks() == 1)
                NativeBridge.nativeLog("INFO", "fixture END_SERVER_TICK");
            if (server.getTicks() == 1)
                NativeBridge.nativeExecuteCommand("cppfm_probe 8");
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
        ModEvents.CHAT.register((player, message) -> message);
    }
}
