package net.minecraft.command.argument;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;
import com.mojang.brigadier.suggestion.SuggestionsBuilder;
import java.util.Collection;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.command.CommandRegistryAccess;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.registry.Registries;
import net.minecraft.state.property.Property;
import net.minecraft.util.Identifier;

/** Brigadier parser for the vanilla block-state argument surface. */
public final class BlockStateArgumentType implements ArgumentType<BlockStateArgument> {
    private static final Collection<String> EXAMPLES = List.of("minecraft:stone", "minecraft:redstone_wire");
    @SuppressWarnings("unused") private final CommandRegistryAccess registryWrapper;

    public BlockStateArgumentType(CommandRegistryAccess commandRegistryAccess) {
        this.registryWrapper = commandRegistryAccess;
    }

    public static BlockStateArgumentType blockState(CommandRegistryAccess commandRegistryAccess) {
        return new BlockStateArgumentType(commandRegistryAccess);
    }

    @Override public BlockStateArgument parse(StringReader reader) throws CommandSyntaxException {
        reader.skipWhitespace();
        String token = reader.readUnquotedString();
        if (token.isEmpty()) throw new CommandSyntaxException("Expected block", reader, reader.getCursor());

        String blockToken = token;
        String propertiesToken = null;
        int open = token.indexOf('[');
        if (open >= 0) {
            if (!token.endsWith("]"))
                throw new CommandSyntaxException("Unclosed block properties", reader, reader.getCursor());
            blockToken = token.substring(0, open);
            propertiesToken = token.substring(open + 1, token.length() - 1);
        }
        int nbt = blockToken.indexOf('{');
        if (nbt >= 0) blockToken = blockToken.substring(0, nbt);
        Identifier id = Identifier.tryParse(blockToken);
        Block block = id == null ? null : Registries.BLOCK.get(id);
        if (block == null)
            throw new CommandSyntaxException("Unknown block: " + blockToken, reader, reader.getCursor());

        BlockState state = block.getDefaultState();
        Set<String> parsedProperties = new LinkedHashSet<>();
        if (propertiesToken != null && !propertiesToken.isEmpty()) {
            for (String entry : propertiesToken.split(",")) {
                String[] pair = entry.split("=", 2);
                if (pair.length != 2) throw new CommandSyntaxException("Invalid block property", reader, reader.getCursor());
                Property<?> property = findProperty(block, pair[0]);
                if (property == null) throw new CommandSyntaxException("Unknown block property: " + pair[0], reader, reader.getCursor());
                Object value = property.parse(pair[1]).orElse(null);
                if (!(value instanceof Comparable<?>))
                    throw new CommandSyntaxException("Invalid block property value", reader, reader.getCursor());
                @SuppressWarnings({"rawtypes", "unchecked"})
                Property raw = property;
                state = state.with(raw, (Comparable) value);
                parsedProperties.add(pair[0]);
            }
        }
        return new BlockStateArgument(state, parsedProperties, new NbtCompound());
    }

    private static Property<?> findProperty(Block block, String name) {
        if (block == null || name == null) return null;
        for (Property<?> property : block.getStateManager().getProperties())
            if (name.equals(property.getName())) return property;
        return null;
    }

    public static BlockState getBlockState(CommandContext<?> context, String name) {
        if (context == null) throw new IllegalArgumentException("context");
        BlockStateArgument argument = context.getArgument(name, BlockStateArgument.class);
        if (argument != null) return argument.getBlockState();
        BlockState state = context.getArgument(name, BlockState.class);
        if (state != null) return state;
        throw new IllegalArgumentException("argument is not a block state: " + name);
    }

    @Override public <S> CompletableFuture<com.mojang.brigadier.suggestion.Suggestions> listSuggestions(
            CommandContext<S> context, SuggestionsBuilder builder) {
        return CompletableFuture.completedFuture(builder.build());
    }

    @Override public Collection<String> getExamples() { return EXAMPLES; }
}
