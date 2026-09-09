package net.fabricmc.fabric.api.object.builder.v1.block.entity;

import net.minecraft.block.Block;

/** Access-widened hook for adding supported blocks to a block-entity type. */
public interface FabricBlockEntityType {
    default void addSupportedBlock(Block block) { }
}
