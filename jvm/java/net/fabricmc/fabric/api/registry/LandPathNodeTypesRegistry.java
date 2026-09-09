package net.fabricmc.fabric.api.registry;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.entity.ai.pathing.PathNodeType;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.BlockView;

/** Per-block path-node providers used by navigation-aware mods. */
public final class LandPathNodeTypesRegistry {
    private static final Map<Block, PathNodeTypeProvider> NODE_TYPES = new ConcurrentHashMap<>();
    private LandPathNodeTypesRegistry() { }

    public static void register(Block block, PathNodeType walkable, PathNodeType defaultType) {
        if (block == null) throw new NullPointerException("Block cannot be null!");
        register(block, (state, canOpenDoors) -> canOpenDoors ? walkable : defaultType);
    }
    public static void register(Block block, StaticPathNodeTypeProvider provider) {
        if (block == null || provider == null) throw new NullPointerException("block/provider");
        NODE_TYPES.put(block, provider);
    }
    public static void registerDynamic(Block block, DynamicPathNodeTypeProvider provider) {
        if (block == null || provider == null) throw new NullPointerException("block/provider");
        NODE_TYPES.put(block, provider);
    }
    public static PathNodeType getPathNodeType(BlockState state, BlockView view, BlockPos pos,
                                               boolean canOpenDoors) {
        if (state == null || view == null || pos == null) throw new NullPointerException("state/view/pos");
        PathNodeTypeProvider provider = NODE_TYPES.get(state.getBlock());
        if (provider instanceof DynamicPathNodeTypeProvider dynamic)
            return dynamic.getPathNodeType(state, view, pos, canOpenDoors);
        return provider instanceof StaticPathNodeTypeProvider stat
            ? stat.getPathNodeType(state, canOpenDoors) : null;
    }
    public static PathNodeTypeProvider getPathNodeTypeProvider(Block block) {
        if (block == null) throw new NullPointerException("Block cannot be null!");
        return NODE_TYPES.get(block);
    }
    public interface PathNodeTypeProvider { }
    public interface StaticPathNodeTypeProvider extends PathNodeTypeProvider {
        PathNodeType getPathNodeType(BlockState state, boolean canOpenDoors);
    }
    public interface DynamicPathNodeTypeProvider extends PathNodeTypeProvider {
        PathNodeType getPathNodeType(BlockState state, BlockView view, BlockPos pos, boolean canOpenDoors);
    }
}
