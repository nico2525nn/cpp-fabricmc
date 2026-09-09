package net.minecraft.world;

import net.minecraft.block.BlockState;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.Vec3d;

public interface BlockView {
    BlockState getBlockState(BlockPos pos);
    /** Intermediary raycast helper used by server-side optimization mixins. */
    default net.minecraft.util.hit.BlockHitResult method_17743(RaycastContext context, BlockPos pos) {
        return raycast(context);
    }
    default net.minecraft.block.entity.BlockEntity getBlockEntity(BlockPos pos) { return null; }
    default net.minecraft.util.hit.BlockHitResult raycast(RaycastContext context) {
        if (context == null) return null;
        Vec3d start = context.getStart();
        Vec3d end = context.getEnd();
        double distance = start.distanceTo(end);
        int steps = Math.max(1, Math.min(256, (int) Math.ceil(distance * 8.0)));
        BlockPos last = null;
        for (int i = 0; i <= steps; ++i) {
            double t = (double) i / steps;
            BlockPos pos = BlockPos.ofFloored(
                start.x + (end.x - start.x) * t,
                start.y + (end.y - start.y) * t,
                start.z + (end.z - start.z) * t);
            if (pos.equals(last)) continue;
            last = pos;
            BlockState state = getBlockState(pos);
            if (state != null && !state.isAir())
                return new net.minecraft.util.hit.BlockHitResult(
                    new Vec3d(pos.getX() + 0.5, pos.getY() + 0.5, pos.getZ() + 0.5),
                    Direction.fromVector((int) Math.signum(end.x - start.x),
                        (int) Math.signum(end.y - start.y), (int) Math.signum(end.z - start.z)).getOpposite(),
                    pos, false);
        }
        return net.minecraft.util.hit.BlockHitResult.createMissed(end, Direction.UP,
            BlockPos.ofFloored(end.x, end.y, end.z));
    }
}
