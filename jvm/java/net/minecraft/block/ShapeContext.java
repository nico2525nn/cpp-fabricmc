package net.minecraft.block;

import net.minecraft.entity.Entity;
import net.minecraft.fluid.FluidState;
import net.minecraft.item.Item;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.shape.VoxelShape;
import net.minecraft.util.shape.VoxelShapes;
import net.minecraft.world.CollisionView;

/** 1.21.4 collision-context ABI used by block and world mixins. */
public interface ShapeContext {
    static ShapeContext of(Entity entity) { return of(entity, false); }
    static ShapeContext of(Entity entity, boolean collidesWithFluid) {
        return new ShapeContext() {
            @Override public boolean isHolding(Item item) { return false; }
            @Override public boolean canWalkOnFluid(FluidState state, FluidState above) {
                return collidesWithFluid && state != null && above != null;
            }
        };
    }
    static ShapeContext absent() { return new ShapeContext() {}; }

    default boolean canWalkOnFluid(FluidState state, FluidState stateAbove) { return false; }
    default boolean isAbove(VoxelShape shape, BlockPos pos, boolean defaultValue) { return defaultValue; }
    default boolean isDescending() { return false; }
    default VoxelShape getCollisionShape(BlockState state, CollisionView world, BlockPos pos) {
        return state == null ? VoxelShapes.EMPTY : state.getCollisionShape(world, pos, this);
    }
    default boolean isHolding(Item item) { return false; }
}
