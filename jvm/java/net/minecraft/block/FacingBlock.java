package net.minecraft.block;

import net.minecraft.state.property.EnumProperty;
import net.minecraft.util.math.Direction;

/** Shared facing-block ABI; vanilla subclasses expose the same property. */
public class FacingBlock extends Block {
    public static final EnumProperty<Direction> FACING = EnumProperty.of("facing", Direction.class);
    /** Intermediary owner used by subclasses before inherited-member lookup. */
    public static final EnumProperty<Direction> field_10927 = FACING;

    protected FacingBlock(AbstractBlock.Settings settings) { super(settings); }
    protected FacingBlock() { super(AbstractBlock.Settings.create()); }
}
