package net.minecraft.block;

import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.state.property.BooleanProperty;
import net.minecraft.state.property.EnumProperty;
import net.minecraft.world.World;

/** 1.21.4 piston block ABI used by allocation and behavior mixins. */
public class PistonBlock extends FacingBlock {
    public static final BooleanProperty EXTENDED = BooleanProperty.of("extended");
    public static final EnumProperty<Direction> field_10927 = FacingBlock.field_10927;
    private final boolean sticky;

    public PistonBlock(boolean sticky, AbstractBlock.Settings settings) {
        super(settings);
        this.sticky = sticky;
    }

    public PistonBlock(boolean sticky) { this(sticky, AbstractBlock.Settings.create()); }
    public boolean isSticky() { return sticky; }
    public boolean isMovable(BlockState state, World world, BlockPos pos, Direction direction,
                             boolean canBreak, Direction pistonDir) { return true; }
    public boolean move(World world, BlockPos pos, Direction direction, boolean extend) { return false; }
    public boolean shouldExtend(net.minecraft.world.RedstoneView world, BlockPos pos, Direction pistonFace) {
        return world != null && world.isReceivingRedstonePower(pos);
    }
    /** 1.21.4 neighbour-signal target used by quasi-connectivity mixins. */
    public boolean getNeighborSignal(net.minecraft.world.RedstoneView world, BlockPos pos,
                                     Direction pistonFace) {
        return shouldExtend(world, pos, pistonFace);
    }
    public void tryMove(World world, BlockPos pos, BlockState state) { }
}
