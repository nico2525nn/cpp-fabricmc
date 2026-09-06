package net.minecraft.world;

import java.util.Collections;
import java.util.List;
import java.util.Optional;
import java.util.stream.Stream;
import net.minecraft.block.ShapeContext;
import net.minecraft.block.BlockState;
import net.minecraft.entity.Entity;
import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.util.math.Vec3d;
import net.minecraft.util.shape.VoxelShape;
import net.minecraft.util.shape.VoxelShapes;
import net.minecraft.world.border.WorldBorder;

/**
 * Minimal 1.21.4 collision-view ABI used by server mods and mixin targets.
 *
 * <p>The authoritative collision implementation remains in the native world;
 * these defaults provide a conservative, linkable shadow surface for mods
 * which only inspect the interface or override one of its methods.</p>
 */
public interface CollisionView extends BlockView {
    default Iterable<VoxelShape> getBlockCollisions(Entity entity, Box box) {
        return Collections.emptyList();
    }

    default boolean canCollide(Entity entity, Box box) { return !isSpaceEmpty(entity, box); }
    default VoxelShape method_51716(BlockPos.Mutable pos, VoxelShape shape) { return shape; }
    default Stream<VoxelShape> method_39452(VoxelShape collision) {
        return collision == null || collision.isEmpty() ? Stream.empty() : Stream.of(collision);
    }
    default VoxelShape getWorldBorderCollisions(Entity entity, Box box) { return VoxelShapes.EMPTY; }

    default boolean canPlace(BlockState state, BlockPos pos, ShapeContext context) {
        return state != null && pos != null;
    }
    default List<VoxelShape> getEntityCollisions(Entity entity, Box box) {
        return List.of();
    }

    default boolean canPlace(BlockState state, BlockPos pos, Object context) {
        return state != null && pos != null;
    }

    default boolean doesNotIntersectEntities(Entity entity, VoxelShape shape) {
        return shape == null || shape == VoxelShapes.EMPTY || shape.isEmpty();
    }

    default boolean isSpaceEmpty(Box box) { return getCollisions(box).iterator().hasNext() == false; }
    default boolean isSpaceEmpty(Entity entity, Box box, boolean checkFluid) { return isSpaceEmpty(entity, box); }
    default BlockHitResult getCollisionsIncludingWorldBorder(RaycastContext context) {
        return new BlockHitResult(BlockPos.ofFloored(context == null ? Vec3d.ZERO.getX() : context.getEnd().getX(),
                                                    context == null ? 0 : context.getEnd().getY(),
                                                    context == null ? Vec3d.ZERO.getZ() : context.getEnd().getZ()));
    }
    default boolean isBlockSpaceEmpty(Entity entity, Box box) { return isSpaceEmpty(entity, box); }
    default BlockView getChunkAsView(int chunkX, int chunkZ) { return this; }
    default boolean isSpaceEmpty(Entity entity, Box box) {
        return getEntityCollisions(entity, box).isEmpty();
    }
    default VoxelShape method_51717(BlockPos.Mutable pos, VoxelShape shape) { return shape; }
    default WorldBorder getWorldBorder() { return new WorldBorder(); }
    default boolean isSpaceEmpty(Entity entity) { return isSpaceEmpty(entity, entity == null ? null : entity.getBoundingBox()); }
    default boolean method_39453(VoxelShape collision) { return collision == null || collision.isEmpty(); }
    default BlockPos method_51715(BlockPos.Mutable pos, VoxelShape shape) { return pos == null ? null : pos.toImmutable(); }
    default Optional<Vec3d> findClosestCollision(Entity entity, VoxelShape shape, Vec3d target,
                                                  double x, double y, double z) { return Optional.empty(); }
    default Box method_39451(double x, double y, double z, Box box) {
        return box == null ? null : box.offset(x, y, z);
    }
    default boolean doesNotIntersectEntities(Entity entity) { return true; }
    default Iterable<VoxelShape> getBlockOrFluidCollisions(Entity entity, Box box) {
        return getBlockCollisions(entity, box);
    }
    default Optional<BlockPos> findSupportingBlockPos(Entity entity, Box box) { return Optional.empty(); }
    default Iterable<VoxelShape> getCollisions(Box box) { return getBlockCollisions(null, box); }
}
