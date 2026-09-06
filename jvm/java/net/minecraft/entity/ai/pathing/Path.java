package net.minecraft.entity.ai.pathing;

import net.minecraft.util.math.BlockPos;

/** Minimal linkable path value used by the 1.21.4 navigation ABI. */
public class Path {
    private final BlockPos target;
    private final boolean reachesTarget;

    public Path() { this(null, false); }
    public Path(BlockPos target, boolean reachesTarget) {
        this.target = target;
        this.reachesTarget = reachesTarget;
    }
    public BlockPos getTarget() { return target; }
    public boolean reachesTarget() { return reachesTarget; }
    public boolean isFinished() { return target == null; }
}
