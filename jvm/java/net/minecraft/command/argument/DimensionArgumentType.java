package net.minecraft.command.argument;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;
import java.util.Collection;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import com.mojang.brigadier.suggestion.Suggestions;
import com.mojang.brigadier.suggestion.SuggestionsBuilder;
import net.minecraft.server.command.ServerCommandSource;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.Identifier;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.RegistryKeys;

/** Argument and lookup helpers for a server dimension identifier. */
public final class DimensionArgumentType implements ArgumentType<Identifier> {
    private static final Collection<String> EXAMPLES = List.of("minecraft:overworld", "minecraft:the_nether");

    private DimensionArgumentType() { }

    public static DimensionArgumentType dimension() { return new DimensionArgumentType(); }

    @Override public Identifier parse(StringReader reader) throws CommandSyntaxException {
        String value = reader.readString();
        Identifier identifier = Identifier.tryParse(value);
        if (identifier == null)
            throw new CommandSyntaxException("Invalid dimension: " + value, reader, reader.getCursor());
        return identifier;
    }

    public static ServerWorld getDimensionArgument(CommandContext<ServerCommandSource> context, String name) {
        Identifier identifier = context == null ? null : context.getArgument(name, Identifier.class);
        ServerCommandSource source = context == null ? null : context.getSource();
        if (source == null || source.getServer() == null) return source == null ? null : source.getWorld();
        if (identifier == null || identifier.equals(Identifier.ofVanilla("overworld"))) return source.getServerWorld();
        return source.getServer().getWorld(RegistryKey.of(RegistryKeys.WORLD, identifier));
    }

    @Override public <S> CompletableFuture<Suggestions> listSuggestions(CommandContext<S> context, SuggestionsBuilder builder) {
        return CompletableFuture.completedFuture(builder.build());
    }

    @Override public Collection<String> getExamples() { return EXAMPLES; }
}
