package net.minecraft.command.argument;

import com.mojang.brigadier.StringReader;
import com.mojang.brigadier.arguments.ArgumentType;
import com.mojang.brigadier.context.CommandContext;
import com.mojang.brigadier.exceptions.CommandSyntaxException;
import java.util.Collection;
import java.util.List;
import net.minecraft.command.CommandSource;
import net.minecraft.server.command.ServerCommandSource;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec2f;
import net.minecraft.util.math.Vec3d;

/** Two-angle rotation argument used by the player command. */
public final class RotationArgumentType implements ArgumentType<PosArgument> {
    private static final Collection<String> EXAMPLES = List.of("0 0", "~ ~", "90 45");

    private RotationArgumentType() { }

    public static RotationArgumentType rotation() { return new RotationArgumentType(); }

    @Override public PosArgument parse(StringReader reader) throws CommandSyntaxException {
        float yaw = angle(reader, "yaw");
        float pitch = angle(reader, "pitch");
        return new ParsedRotation(yaw, pitch);
    }

    private static float angle(StringReader reader, String name) throws CommandSyntaxException {
        String token = reader.readString();
        if (token == null || token.isEmpty())
            throw new CommandSyntaxException("Expected " + name, reader, reader.getCursor());
        try {
            boolean relative = token.charAt(0) == '~';
            String number = relative ? token.substring(1) : token;
            return Float.parseFloat(number.isEmpty() ? "0" : number);
        } catch (NumberFormatException error) {
            throw new CommandSyntaxException("Invalid " + name + ": " + token, error,
                reader, reader.getCursor());
        }
    }

    public static PosArgument getRotation(CommandContext<?> context, String name) {
        PosArgument result = context == null ? null : context.getArgument(name, PosArgument.class);
        if (result == null) throw new IllegalArgumentException("argument is not a rotation: " + name);
        return result;
    }

    @Override public Collection<String> getExamples() { return EXAMPLES; }

    private static final class ParsedRotation implements PosArgument {
        private final float yaw;
        private final float pitch;

        private ParsedRotation(float yaw, float pitch) { this.yaw = yaw; this.pitch = pitch; }
        @Override public Vec3d getPos(CommandSource source) {
            return source instanceof ServerCommandSource serverSource ? serverSource.getPosition() : Vec3d.ZERO;
        }
        @Override public Vec2f getRotation(CommandSource source) { return new Vec2f(yaw, pitch); }
        @Override public BlockPos toAbsoluteBlockPos(CommandSource source) {
            Vec3d position = getPos(source);
            return BlockPos.ofFloored(position.getX(), position.getY(), position.getZ());
        }
        @Override public boolean isXRelative() { return false; }
        @Override public boolean isYRelative() { return false; }
        @Override public boolean isZRelative() { return false; }
    }
}
