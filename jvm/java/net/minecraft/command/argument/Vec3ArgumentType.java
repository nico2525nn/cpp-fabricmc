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
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec2f;
import net.minecraft.util.math.Vec3d;

/** Three-dimensional coordinate argument used by server commands. */
public final class Vec3ArgumentType implements ArgumentType<PosArgument> {
    private static final Collection<String> EXAMPLES = List.of("0.0 0.0 0.0", "~ ~ ~", "~1.5 ~-2 ~3");
    private final boolean centerIntegers;

    private Vec3ArgumentType(boolean centerIntegers) { this.centerIntegers = centerIntegers; }

    public static Vec3ArgumentType vec3() { return new Vec3ArgumentType(false); }
    public static Vec3ArgumentType vec3(boolean centerIntegers) { return new Vec3ArgumentType(centerIntegers); }

    @Override public PosArgument parse(StringReader reader) throws CommandSyntaxException {
        Coordinate x = coordinate(reader, "x");
        Coordinate y = coordinate(reader, "y");
        Coordinate z = coordinate(reader, "z");
        return new ParsedPosition(x, y, z, centerIntegers);
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

    public static PosArgument getPosArgument(CommandContext<?> context, String name) {
        PosArgument result = context == null ? null : context.getArgument(name, PosArgument.class);
        if (result == null) throw new IllegalArgumentException("argument is not a position: " + name);
        return result;
    }

    public static Vec3d getVec3(CommandContext<?> context, String name) {
        PosArgument argument = getPosArgument(context, name);
        CommandSource source = context.getSource() instanceof CommandSource value ? value : null;
        return argument.getPos(source);
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
        private final boolean centerIntegers;

        private ParsedPosition(Coordinate x, Coordinate y, Coordinate z, boolean centerIntegers) {
            this.x = x; this.y = y; this.z = z; this.centerIntegers = centerIntegers;
        }

        @Override public Vec3d getPos(CommandSource source) {
            Vec3d origin = source instanceof ServerCommandSource serverSource
                ? serverSource.getPosition() : Vec3d.ZERO;
            return new Vec3d(value(x, origin.getX()), value(y, origin.getY()), value(z, origin.getZ()));
        }

        private double value(Coordinate coordinate, double origin) {
            double value = coordinate.relative ? origin + coordinate.value : coordinate.value;
            return coordinate.relative || !centerIntegers ? value : value + 0.5;
        }

        @Override public Vec2f getRotation(CommandSource source) {
            return source instanceof ServerCommandSource serverSource ? serverSource.getRotation() : Vec2f.ZERO;
        }

        @Override public BlockPos toAbsoluteBlockPos(CommandSource source) {
            Vec3d position = getPos(source);
            return BlockPos.ofFloored(position.getX(), position.getY(), position.getZ());
        }

        @Override public boolean isXRelative() { return x.relative; }
        @Override public boolean isYRelative() { return y.relative; }
        @Override public boolean isZRelative() { return z.relative; }
    }
}
