package cppfm.fixture;

import cppfm.bridge.NativeBridge;
import cppfm.api.ModEvents;
import net.fabricmc.api.DedicatedServerModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
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
import net.minecraft.server.world.ServerWorld;
import net.minecraft.server.command.CommandManager;
import net.minecraft.server.command.ServerCommandSource;
import com.mojang.brigadier.arguments.IntegerArgumentType;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.profiler.Profiler;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executor;

/** Deterministic integration fixture for the embedded loader and callbacks. */
public final class FixtureMod implements DedicatedServerModInitializer {
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
            if (server.getTicks() == 1) {
                NativeBridge.nativeExecuteCommand("cppfm_probe 8");
                boolean reloadResult = NativeBridge.nativeExecuteCommand("reload");
                NativeBridge.nativeLog("INFO", "fixture RELOAD_COMMAND_RESULT " + reloadResult);
            }
            if (server.getTicks() == 2)
                NativeBridge.nativeLog("INFO", "fixture POST_RELOAD_TICK");
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
