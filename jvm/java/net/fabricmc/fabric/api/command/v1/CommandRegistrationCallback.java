package net.fabricmc.fabric.api.command.v1;

import com.mojang.brigadier.CommandDispatcher;
import net.fabricmc.fabric.api.event.Event;
import net.fabricmc.fabric.api.event.EventFactory;
import net.minecraft.server.command.ServerCommandSource;

/** Legacy two-argument command registration callback. */
@FunctionalInterface
public interface CommandRegistrationCallback {
    Event<CommandRegistrationCallback> EVENT = EventFactory.createArrayBacked(
        CommandRegistrationCallback.class,
        callbacks -> (dispatcher, dedicated) -> {
            for (CommandRegistrationCallback callback : callbacks)
                callback.register(dispatcher, dedicated);
        });

    void register(CommandDispatcher<ServerCommandSource> dispatcher, boolean dedicated);
}
