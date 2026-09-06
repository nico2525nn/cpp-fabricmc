package net.minecraft.command.argument;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;
import java.util.Collection;
import java.util.List;
import net.minecraft.world.GameMode;

/** Brigadier argument for the four vanilla game modes. */
public final class GameModeArgumentType implements ArgumentType<GameMode> {
    private static final Collection<String> EXAMPLES = List.of("survival", "creative", "adventure", "spectator");

    private GameModeArgumentType() { }

    public static GameModeArgumentType gameMode() { return new GameModeArgumentType(); }

    @Override public GameMode parse(StringReader reader) throws CommandSyntaxException {
        String value = reader.readString();
        GameMode mode = GameMode.byName(value);
        if (mode == null) throw new CommandSyntaxException("Invalid game mode: " + value, reader, reader.getCursor());
        return mode;
    }

    public static GameMode getGameMode(CommandContext<?> context, String name) {
        GameMode result = context == null ? null : context.getArgument(name, GameMode.class);
        if (result == null) throw new IllegalArgumentException("argument is not a game mode: " + name);
        return result;
    }

    @Override public <S> java.util.concurrent.CompletableFuture<com.mojang.brigadier.suggestion.Suggestions> listSuggestions(
            CommandContext<S> context, com.mojang.brigadier.suggestion.SuggestionsBuilder builder) {
        return java.util.concurrent.CompletableFuture.completedFuture(builder.build());
    }

    @Override public Collection<String> getExamples() { return EXAMPLES; }
}
