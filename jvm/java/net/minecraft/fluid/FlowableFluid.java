package net.minecraft.fluid;

import java.util.Map;
import net.minecraft.state.property.BooleanProperty;
import net.minecraft.state.property.IntProperty;
import net.minecraft.util.Identifier;

/** Source-compatible flowing-fluid base used by Fabric mixin targets. */
public class FlowableFluid extends Fluid {
    public static final IntProperty LEVEL = IntProperty.of("level", 1, 8);
    public static final BooleanProperty FALLING = BooleanProperty.of("falling");

    protected FlowableFluid() { this(Identifier.of("minecraft", "empty")); }
    public FlowableFluid(Identifier id) { super(id); }

    public FluidState getStill() { return getDefaultState(); }
    public FluidState getStill(boolean falling) { return getStill(); }
    public FluidState getFlowing() { return getDefaultState(); }
    public FluidState getFlowing(int level, boolean falling) { return getFlowing(); }
    public boolean isInfinite(net.minecraft.world.WorldView world) { return false; }
    public int getLevelDecreasePerBlock(net.minecraft.world.WorldView world) { return 1; }
    public int getMaxFlowDistance(net.minecraft.world.WorldView world) { return 4; }
    public int getNextTickDelay(net.minecraft.world.World world, net.minecraft.util.math.BlockPos pos,
                                FluidState oldState, FluidState newState) { return 5; }
    public Map<?, ?> getSpread(net.minecraft.server.world.ServerWorld world,
                               net.minecraft.util.math.BlockPos pos,
                               net.minecraft.block.BlockState state) { return Map.of(); }

    /** Lightweight nested type retained for descriptor compatibility. */
    public static class SpreadCache {
        public SpreadCache(FlowableFluid fluid, net.minecraft.world.BlockView world,
                           net.minecraft.util.math.BlockPos startPos) {}
    }

    /** Lightweight value holder retained for descriptor compatibility. */
    public static record NeighborGroup(net.minecraft.block.BlockState self,
                                       net.minecraft.block.BlockState other,
                                       net.minecraft.util.math.Direction facing) {}
}
