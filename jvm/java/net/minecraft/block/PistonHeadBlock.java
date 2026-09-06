package net.minecraft.block;

import net.minecraft.state.property.BooleanProperty;
import net.minecraft.state.property.EnumProperty;
import net.minecraft.util.math.Direction;

/** 1.21.4 piston-head block ABI used by Lithium and Carpet mixins. */
public class PistonHeadBlock extends FacingBlock {
    public static final EnumProperty<net.minecraft.block.enums.PistonType> TYPE =
        EnumProperty.of("type", net.minecraft.block.enums.PistonType.class);
    public static final BooleanProperty SHORT = BooleanProperty.of("short");

    public PistonHeadBlock(AbstractBlock.Settings settings) { super(settings); }
    public PistonHeadBlock() { this(AbstractBlock.Settings.create()); }

    public static boolean isAttached(BlockState headState, BlockState pistonState) {
        return headState != null && pistonState != null;
    }
    public static net.minecraft.util.shape.VoxelShape getHeadShape(Direction direction, boolean shortHead) {
        return net.minecraft.util.shape.VoxelShapes.fullCube();
    }
    public static net.minecraft.util.shape.VoxelShape[] getHeadShapes(boolean shortHead) {
        return new net.minecraft.util.shape.VoxelShape[] {
            getHeadShape(Direction.DOWN, shortHead), getHeadShape(Direction.UP, shortHead),
            getHeadShape(Direction.NORTH, shortHead), getHeadShape(Direction.SOUTH, shortHead),
            getHeadShape(Direction.WEST, shortHead), getHeadShape(Direction.EAST, shortHead)
        };
    }
    public static net.minecraft.util.shape.VoxelShape method_31020(Direction direction) {
        return getHeadShape(direction, false);
    }
}
