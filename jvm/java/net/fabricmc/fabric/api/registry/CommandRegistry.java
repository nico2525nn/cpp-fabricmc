package net.fabricmc.fabric.api.registry;

import com.mojang.brigadier.CommandDispatcher;
import java.util.function.Consumer;
import net.fabricmc.fabric.api.command.v2.CommandRegistrationCallback;
import net.minecraft.server.command.ServerCommandSource;

/** Compatibility facade for Fabric's legacy command registry. */
public class CommandRegistry {
    public static final CommandRegistry INSTANCE = new CommandRegistry();
    public CommandRegistry() { }

    public void register(boolean dedicated,
                         Consumer<CommandDispatcher<ServerCommandSource>> callback) {
        if (callback == null) return;
        CommandRegistrationCallback.EVENT.register((dispatcher, registry, environment) -> {
            if (dedicated && environment != net.minecraft.server.command.CommandManager.RegistrationEnvironment.DEDICATED)
                return;
            callback.accept(dispatcher);
        });
    }
}
