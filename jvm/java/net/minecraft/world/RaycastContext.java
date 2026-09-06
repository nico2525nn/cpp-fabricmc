package net.minecraft.world;

import net.minecraft.block.BlockState;
import net.minecraft.block.ShapeContext;
import net.minecraft.fluid.FluidState;
import net.minecraft.entity.Entity;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec3d;
import net.minecraft.util.shape.VoxelShape;

/** Minimal raycast context with the named 1.21.4 nested enum ABI. */
public class RaycastContext {
    private final Vec3d start;
    private final Vec3d end;
    private final ShapeType shapeType;
    private final FluidHandling fluidHandling;
    private final ShapeContext shapeContext;

    public RaycastContext(Vec3d start, Vec3d end, ShapeType shapeType,
                          FluidHandling fluidHandling, Entity entity) {
        this(start, end, shapeType, fluidHandling, ShapeContext.of(entity));
    }
    public RaycastContext(Vec3d start, Vec3d end, ShapeType shapeType,
                          FluidHandling fluidHandling, ShapeContext shapeContext) {
        this.start = start == null ? Vec3d.ZERO : start;
        this.end = end == null ? this.start : end;
        this.shapeType = shapeType == null ? ShapeType.COLLIDER : shapeType;
        this.fluidHandling = fluidHandling == null ? FluidHandling.NONE : fluidHandling;
        this.shapeContext = shapeContext == null ? ShapeContext.absent() : shapeContext;
    }
    public Vec3d getStart() { return start; }
    public Vec3d getEnd() { return end; }
    public ShapeType getShapeType() { return shapeType; }
    public FluidHandling getFluidHandling() { return fluidHandling; }
    public ShapeContext getShapeContext() { return shapeContext; }

    @FunctionalInterface
    public interface ShapeProvider {
        VoxelShape get(BlockState state, BlockView world, BlockPos pos, ShapeContext context);
    }

    public enum ShapeType {
        COLLIDER((state, world, pos, context) -> state == null ? net.minecraft.util.shape.VoxelShapes.EMPTY : state.getCollisionShape(world, pos, context)),
        OUTLINE((state, world, pos, context) -> state == null ? net.minecraft.util.shape.VoxelShapes.EMPTY : state.getOutlineShape(world, pos, context)),
        FALLDAMAGE_RESETTING((state, world, pos, context) -> state == null ? net.minecraft.util.shape.VoxelShapes.EMPTY : state.getCollisionShape(world, pos, context)),
        VISUAL((state, world, pos, context) -> state == null ? net.minecraft.util.shape.VoxelShapes.EMPTY : state.getOutlineShape(world, pos, context));

        private final ShapeProvider provider;
        ShapeType(ShapeProvider provider) { this.provider = provider; }
        public VoxelShape getProvider(BlockState state, BlockView world, BlockPos pos, ShapeContext context) {
            return provider.get(state, world, pos, context);
        }
    }

    public enum FluidHandling {
        WATER(state -> state != null && state.getFluid() == net.minecraft.fluid.Fluids.WATER),
        SOURCE_ONLY(state -> state != null && state.isStill()),
        ANY(state -> state != null && state.getFluid() != net.minecraft.fluid.Fluids.EMPTY),
        NONE(state -> false);

        private final java.util.function.Predicate<FluidState> predicate;
        FluidHandling(java.util.function.Predicate<FluidState> predicate) { this.predicate = predicate; }
        public boolean matches(FluidState state) { return predicate.test(state); }
    }
}
