package net.minecraft.command.argument;

import net.minecraft.command.CommandSource;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec2f;
import net.minecraft.util.math.Vec3d;

/** Position value produced by the coordinate argument types. */
public interface PosArgument {
    Vec3d getPos(CommandSource source);
    Vec2f getRotation(CommandSource source);
    BlockPos toAbsoluteBlockPos(CommandSource source);
    boolean isXRelative();
    boolean isYRelative();
    boolean isZRelative();
}
