package net.minecraft.server.command;

import com.mojang.brigadier.CommandDispatcher;
import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.builder.LiteralArgumentBuilder;
import com.mojang.brigadier.builder.RequiredArgumentBuilder;

public class CommandManager {
    public enum RegistrationEnvironment {
        ALL(true, true),
        DEDICATED(false, true),
        INTEGRATED(true, false);

        // These are intentionally fields, not derived methods: Fabric's
        // transitive access widener targets the 1.21.4 named field symbols.
        private final boolean integrated;
        private final boolean dedicated;

        RegistrationEnvironment(boolean integrated, boolean dedicated) {
            this.integrated = integrated;
            this.dedicated = dedicated;
        }
    }
    private final CommandDispatcher<ServerCommandSource> dispatcher = new CommandDispatcher<>();
    public CommandManager() {
        this(RegistrationEnvironment.DEDICATED, new net.minecraft.command.CommandRegistryAccess());
    }
    /** 1.21.4 constructor shape used by Minecraft and command-registration mixins. */
    public CommandManager(RegistrationEnvironment environment,
                          net.minecraft.command.CommandRegistryAccess commandRegistryAccess) {
        // Vanilla populates the dispatcher between construction and return.
        // The compatibility transformer uses this exact constructor boundary
        // for mods such as Carpet that register commands at RETURN.
    }
    public CommandDispatcher<ServerCommandSource> getDispatcher() { return dispatcher; }
    public int execute(String command, ServerCommandSource source) {
        try { return dispatcher.execute(command, source); }
        catch (Exception ignored) { return 0; }
    }
    /** Vanilla's command execution shell; Carpet injects its lifecycle hooks here. */
    private void performCommand(com.mojang.brigadier.ParseResults<?> parseResults, String command) {
        // The native command ingress owns execution.  Keeping this method as a
        // straight-line shell gives Mixin HEAD/RETURN hooks a real target while
        // avoiding a duplicate dispatch or a second exception path.
    }
    /** Yarn-mapped command execution hook targeted by Carpet's refmap. */
    private void execute(com.mojang.brigadier.ParseResults<ServerCommandSource> parseResults,
                         String command) {
        if (parseResults == null) return;
        try { dispatcher.execute(parseResults); }
        catch (Exception ignored) { }
    }
    /**
     * Return whether the command tree contains the requested root literal.
     *
     * <p>The production launch path intentionally resolves Brigadier from the
     * locked server libraries before the shadow classes.  Brigadier's public
     * tree API is stable across the supported 1.21.4 builds, while the
     * project's convenience {@code hasCommand(String)} helper is not part of
     * Brigadier's upstream API.  Calling that helper here therefore creates a
     * runtime-only {@code NoSuchMethodError} when a real server Brigadier jar
     * is present.  Minecraft uses this method as a cheap command-ingress
     * guard, so checking the root literal preserves the intended behavior and
     * keeps the call site ABI-safe.</p>
     */
    public boolean hasCommand(String command) {
        if (command == null) return false;
        String normalized = command.trim();
        if (normalized.isEmpty()) return false;
        int separator = normalized.indexOf(' ');
        String rootName = separator < 0 ? normalized : normalized.substring(0, separator);
        if (rootName.startsWith("/")) rootName = rootName.substring(1);
        return dispatcher.getRoot().getChild(rootName) != null;
    }
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
