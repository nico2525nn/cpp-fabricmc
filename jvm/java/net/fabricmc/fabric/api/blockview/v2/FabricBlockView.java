package net.fabricmc.fabric.api.blockview.v2;

import net.minecraft.block.entity.BlockEntity;
import net.minecraft.registry.entry.RegistryEntry;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.BlockView;
import net.minecraft.world.biome.Biome;

/** Server-safe extension points used by Fabric's block-view API. */
public interface FabricBlockView {
    default Object getBlockEntityRenderData(BlockPos pos) {
        if (!(this instanceof BlockView view)) return null;
        BlockEntity entity = view.getBlockEntity(pos);
        return entity == null ? null : entity.getRenderData();
    }

    default boolean hasBiomes() { return false; }

    default RegistryEntry<Biome> getBiomeFabric(BlockPos pos) { return null; }
}
