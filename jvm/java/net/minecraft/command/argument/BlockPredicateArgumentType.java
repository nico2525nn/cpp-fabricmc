package net.minecraft.command.argument;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;
import com.mojang.brigadier.suggestion.SuggestionsBuilder;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.function.Predicate;
import net.minecraft.block.BlockState;
import net.minecraft.block.pattern.CachedBlockPosition;
import net.minecraft.command.CommandRegistryAccess;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.util.Identifier;

/** Brigadier argument for a block state or block tag predicate. */
public final class BlockPredicateArgumentType implements ArgumentType<BlockPredicateArgumentType.BlockPredicate> {
    private static final Collection<String> EXAMPLES = List.of("minecraft:stone", "#minecraft:buttons");
    private final CommandRegistryAccess registryWrapper;

    public BlockPredicateArgumentType(CommandRegistryAccess commandRegistryAccess) {
        this.registryWrapper = commandRegistryAccess;
    }

    public static BlockPredicateArgumentType blockPredicate(CommandRegistryAccess commandRegistryAccess) {
        return new BlockPredicateArgumentType(commandRegistryAccess);
    }

    @Override public BlockPredicate parse(StringReader reader) throws CommandSyntaxException {
        reader.skipWhitespace();
        String token = reader.readUnquotedString();
        if (token.isEmpty()) throw new CommandSyntaxException("Expected block predicate", reader, reader.getCursor());
        if (token.startsWith("#")) {
            Identifier id = Identifier.tryParse(token.substring(1));
            if (id == null) throw new CommandSyntaxException("Invalid block tag: " + token, reader, reader.getCursor());
            return new TagPredicate(TagKey.of(RegistryKeys.BLOCK, id), Map.of(), new NbtCompound());
        }
        return new StatePredicate(BlockStateArgumentType.blockState(registryWrapper).parse(new StringReader(token)));
    }

    public static Predicate<CachedBlockPosition> getBlockPredicate(CommandContext<?> context, String name) {
        if (context == null) throw new IllegalArgumentException("context");
        BlockPredicate predicate = context.getArgument(name, BlockPredicate.class);
        if (predicate != null) return predicate;
        Object raw = context.getArgumentRaw(name);
        if (raw instanceof Predicate<?> generic) {
            @SuppressWarnings("unchecked") Predicate<CachedBlockPosition> result = (Predicate<CachedBlockPosition>) generic;
            return result;
        }
        throw new IllegalArgumentException("argument is not a block predicate: " + name);
    }

    @Override public <S> CompletableFuture<com.mojang.brigadier.suggestion.Suggestions> listSuggestions(
            CommandContext<S> context, SuggestionsBuilder builder) {
        return CompletableFuture.completedFuture(builder.build());
    }

    @Override public Collection<String> getExamples() { return EXAMPLES; }

    public interface BlockPredicate extends Predicate<CachedBlockPosition> {
        default boolean hasNbt() { return false; }
    }

    public static final class StatePredicate implements BlockPredicate {
        private final BlockState state;
        private final NbtCompound nbt;
        private final Set<String> properties;

        public StatePredicate(BlockStateArgument argument) {
            this(argument == null ? new BlockState(0) : argument.getBlockState(),
                 argument == null ? Set.of() : argument.getProperties(),
                 argument == null ? new NbtCompound() : argument.getData());
        }

        public StatePredicate(BlockState state, Set<String> properties, NbtCompound nbt) {
            this.state = state == null ? new BlockState(0) : state;
            this.properties = properties == null ? Set.of() : Set.copyOf(properties);
            this.nbt = nbt == null ? new NbtCompound() : nbt;
        }

        @Override public boolean test(CachedBlockPosition position) {
            return position != null && state.equals(position.getBlockState());
        }
        @Override public boolean hasNbt() { return !nbt.isEmpty(); }
        public BlockState getState() { return state; }
        public Set<String> getProperties() { return properties; }
        public NbtCompound getNbt() { return nbt.copy(); }
    }

    public static final class TagPredicate implements BlockPredicate {
        private final TagKey<net.minecraft.block.Block> tag;
        private final Map<String, ?> properties;
        private final NbtCompound nbt;

        public TagPredicate(TagKey<net.minecraft.block.Block> tag, Map<String, ?> properties, NbtCompound nbt) {
            this.tag = tag;
            this.properties = properties == null ? Map.of() : Map.copyOf(properties);
            this.nbt = nbt == null ? new NbtCompound() : nbt;
        }

        @Override public boolean test(CachedBlockPosition position) {
            if (position == null || position.getBlockState() == null || tag == null) return false;
            return position.getBlockState().isIn(tag);
        }
        @Override public boolean hasNbt() { return !nbt.isEmpty(); }
        public TagKey<net.minecraft.block.Block> getTag() { return tag; }
        public Map<String, ?> getProperties() { return properties; }
        public NbtCompound getNbt() { return nbt.copy(); }
    }
}
