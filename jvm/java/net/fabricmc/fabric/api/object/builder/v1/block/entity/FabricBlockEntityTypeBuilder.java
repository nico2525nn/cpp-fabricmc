package net.fabricmc.fabric.api.object.builder.v1.block.entity;

import java.util.Collection;
import java.util.LinkedHashSet;
import java.util.Set;
import com.mojang.datafixers.types.Type;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.block.entity.BlockEntity;
import net.minecraft.block.entity.BlockEntityType;
import net.minecraft.util.math.BlockPos;

/** Fluent builder for block-entity types with deterministic supported blocks. */
public final class FabricBlockEntityTypeBuilder<T extends BlockEntity> {
    @FunctionalInterface public interface Factory<T extends BlockEntity> { T create(BlockPos pos, BlockState state); }
    private Factory<? extends T> factory;
    private final Set<Block> blocks = new LinkedHashSet<>();
    private boolean canExecuteCommands;
    private FabricBlockEntityTypeBuilder(Factory<? extends T> factory, Block... blocks) {
        this.factory = factory; addBlocks(blocks);
    }
    public static <T extends BlockEntity> FabricBlockEntityTypeBuilder<T> create(Factory<? extends T> factory, Block... blocks) {
        return new FabricBlockEntityTypeBuilder<>(factory, blocks);
    }
    public FabricBlockEntityTypeBuilder<T> addBlock(Block block) { if (block != null) blocks.add(block); return this; }
    public FabricBlockEntityTypeBuilder<T> addBlocks(Block... values) { if (values != null) for (Block block : values) addBlock(block); return this; }
    public FabricBlockEntityTypeBuilder<T> addBlocks(Collection<? extends Block> values) { if (values != null) for (Block block : values) addBlock(block); return this; }
    public FabricBlockEntityTypeBuilder<T> canPotentiallyExecuteCommands(boolean value) { canExecuteCommands = value; return this; }
    public BlockEntityType<T> build() { return build(null); }
    public BlockEntityType<T> build(Type<?> dataFixerType) {
        return new BlockEntityType<>((pos, state) -> factory == null ? null : factory.create(pos, state), blocks);
    }
}
