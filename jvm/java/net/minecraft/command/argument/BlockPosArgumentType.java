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
import net.minecraft.command.CommandSource;
import net.minecraft.server.command.ServerCommandSource;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec2f;
import net.minecraft.util.math.Vec3d;

/**
 * Vanilla-compatible block position argument surface.  The parser supports
 * absolute coordinates and the common {@code ~} relative form; native command
 * dispatch remains responsible for world-bound validation.
 */
public final class BlockPosArgumentType implements ArgumentType<PosArgument> {
    private static final Collection<String> EXAMPLES = List.of("0 0 0", "~ ~ ~", "~1 ~-2 ~3");

    private BlockPosArgumentType() { }

    public static BlockPosArgumentType blockPos() { return new BlockPosArgumentType(); }

    @Override public PosArgument parse(StringReader reader) throws CommandSyntaxException {
        Coordinate x = coordinate(reader, "x");
        Coordinate y = coordinate(reader, "y");
        Coordinate z = coordinate(reader, "z");
        return new ParsedPosition(x, y, z);
    }

    private static Coordinate coordinate(StringReader reader, String axis) throws CommandSyntaxException {
        String token = reader.readString();
        if (token == null || token.isEmpty())
            throw new CommandSyntaxException("Expected " + axis + " coordinate", reader, reader.getCursor());
        boolean relative = token.charAt(0) == '~';
        String number = relative ? token.substring(1) : token;
        try {
            double value = number.isEmpty() ? 0.0 : Double.parseDouble(number);
            if (!Double.isFinite(value)) throw new NumberFormatException("non-finite coordinate");
            return new Coordinate(value, relative);
        } catch (NumberFormatException error) {
            throw new CommandSyntaxException("Invalid " + axis + " coordinate: " + token, error,
                reader, reader.getCursor());
        }
    }

    public static BlockPos getBlockPos(CommandContext<?> context, String name) {
        PosArgument argument = context == null ? null : context.getArgument(name, PosArgument.class);
        if (argument == null) throw new IllegalArgumentException("argument is not a block position: " + name);
        CommandSource source = context.getSource() instanceof CommandSource value ? value : null;
        return argument.toAbsoluteBlockPos(source);
    }

    public static BlockPos getValidBlockPos(CommandContext<?> context, String name) {
        return getBlockPos(context, name);
    }

    public static BlockPos getLoadedBlockPos(CommandContext<?> context, String name) {
        return getBlockPos(context, name);
    }

    public static BlockPos getLoadedBlockPos(CommandContext<?> context, ServerWorld world, String name) {
        return getBlockPos(context, name);
    }

    @Override public <S> CompletableFuture<Suggestions> listSuggestions(CommandContext<S> context, SuggestionsBuilder builder) {
        return CompletableFuture.completedFuture(builder.build());
    }

    @Override public Collection<String> getExamples() { return EXAMPLES; }

    private record Coordinate(double value, boolean relative) { }

    private static final class ParsedPosition implements PosArgument {
        private final Coordinate x;
        private final Coordinate y;
        private final Coordinate z;

        private ParsedPosition(Coordinate x, Coordinate y, Coordinate z) {
            this.x = x; this.y = y; this.z = z;
        }

        @Override public Vec3d getPos(CommandSource source) {
            Vec3d origin = source instanceof ServerCommandSource serverSource
                ? serverSource.getPosition() : Vec3d.ZERO;
            return new Vec3d(value(x, origin.getX()), value(y, origin.getY()), value(z, origin.getZ()));
        }

        @Override public Vec2f getRotation(CommandSource source) {
            return source instanceof ServerCommandSource serverSource ? serverSource.getRotation() : Vec2f.ZERO;
        }

        @Override public BlockPos toAbsoluteBlockPos(CommandSource source) {
            Vec3d position = getPos(source);
            return BlockPos.ofFloored(position.getX(), position.getY(), position.getZ());
        }

        private static double value(Coordinate coordinate, double origin) {
            return coordinate.relative ? origin + coordinate.value : coordinate.value;
        }

        @Override public boolean isXRelative() { return x.relative; }
        @Override public boolean isYRelative() { return y.relative; }
        @Override public boolean isZRelative() { return z.relative; }
    }
}
