package net.fabricmc.fabric.api.command.v2;

import com.mojang.brigadier.CommandDispatcher;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.server.command.CommandManager;
import net.minecraft.command.CommandRegistryAccess;
import net.minecraft.server.command.ServerCommandSource;

@FunctionalInterface
public interface CommandRegistrationCallback {
    void register(CommandDispatcher<ServerCommandSource> dispatcher,
                  CommandRegistryAccess registryAccess,
                  CommandManager.RegistrationEnvironment environment);
    Event<CommandRegistrationCallback> EVENT = EventFactory.createArrayBacked(
        CommandRegistrationCallback.class, callbacks -> (dispatcher, registryAccess, environment) -> {
            for (CommandRegistrationCallback callback : callbacks)
                callback.register(dispatcher, registryAccess, environment);
        });
    static void clear() { EVENT.clear(); }
}
