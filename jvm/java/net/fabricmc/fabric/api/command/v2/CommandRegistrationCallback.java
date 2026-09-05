package net.fabricmc.fabric.api.command.v2;

import cppfm.bridge.CppModRuntime;
import com.mojang.brigadier.CommandDispatcher;
import net.fabricmc.fabric.api.event.Event;
import net.minecraft.server.command.CommandManager;
import net.minecraft.server.command.CommandRegistryAccess;
import net.minecraft.server.command.ServerCommandSource;

@FunctionalInterface
public interface CommandRegistrationCallback {
    void register(CommandDispatcher<ServerCommandSource> dispatcher,
                  CommandRegistryAccess registryAccess,
                  CommandManager.RegistrationEnvironment environment);
    Event<CommandRegistrationCallback> EVENT = new Event<>(CppModRuntime::registerCommandRegistration);
    static void clear() { EVENT.clear(); }
}
