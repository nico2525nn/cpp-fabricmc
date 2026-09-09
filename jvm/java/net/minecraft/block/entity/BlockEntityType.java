package net.minecraft.block.entity;

import java.util.Set;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.util.math.BlockPos;

/** Lightweight block-entity type descriptor for the vanilla block ABI. */
public class BlockEntityType<T extends BlockEntity> implements net.fabricmc.fabric.api.object.builder.v1.block.entity.FabricBlockEntityType {
    @FunctionalInterface
    public interface BlockEntityFactory<T extends BlockEntity> {
        T create(BlockPos pos, BlockState state);
    }

    private final BlockEntityFactory<T> factory;
    private java.util.Set<Block> blocks;
    public BlockEntityType(BlockEntityFactory<T> factory, Set<Block> blocks) {
        this.factory = factory;
        this.blocks = blocks == null ? Set.of() : Set.copyOf(blocks);
    }
    public T instantiate(BlockPos pos, BlockState state) { return factory == null ? null : factory.create(pos, state); }
    public boolean supports(Block block) { return block != null && blocks.contains(block); }
    @Override public void addSupportedBlock(Block block) {
        if (block != null) {
            java.util.Set<Block> mutable = new java.util.LinkedHashSet<>(blocks);
            mutable.add(block);
            blocks = java.util.Collections.unmodifiableSet(mutable);
        }
    }
}
