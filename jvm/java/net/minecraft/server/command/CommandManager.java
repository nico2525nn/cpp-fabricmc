package net.minecraft.server.command;

import com.mojang.brigadier.CommandDispatcher;
import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.builder.LiteralArgumentBuilder;
import com.mojang.brigadier.builder.RequiredArgumentBuilder;

public class CommandManager {
    public enum RegistrationEnvironment { DEDICATED, INTEGRATED }
    private final CommandDispatcher<ServerCommandSource> dispatcher = new CommandDispatcher<>();
    public CommandDispatcher<ServerCommandSource> getDispatcher() { return dispatcher; }
    public int execute(String command, ServerCommandSource source) {
        try { return dispatcher.execute(command, source); }
        catch (Exception ignored) { return 0; }
    }
    public boolean hasCommand(String command) { return dispatcher.hasCommand(command); }
    public static int execute(ServerCommandSource source, String command) {
        return source == null || source.getServer() == null ? 0 :
            source.getServer().getCommandManager().execute(command, source);
    }
    public static <S> LiteralArgumentBuilder<S> literal(String name) {
        return LiteralArgumentBuilder.literal(name);
    }
    public static <S, T> RequiredArgumentBuilder<S, T> argument(String name, ArgumentType<T> type) {
        return RequiredArgumentBuilder.argument(name, type);
    }
}
